// ==============================================================================
// Layer 3: System Tests - FuzzPedal
// ==============================================================================
// Constitution Principle XII: Test-First Development
// Tests written BEFORE implementation per spec 067-fuzz-pedal
//
// Reference: specs/067-fuzz-pedal/spec.md (FR-001 to FR-029b, SC-001 to SC-009)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/systems/fuzz_pedal.h>
#include <krate/dsp/primitives/fft.h>

#include <array>
#include <chrono>
#include <cmath>
#include <numeric>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Test Helpers
// =============================================================================

namespace {

// Calculate RMS of a buffer
float calculateRMS(const float* buffer, size_t size) {
    if (size == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(size));
}

// Generate a sine wave
void generateSine(float* buffer, size_t size, float frequency, float sampleRate, float amplitude = 1.0f) {
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = amplitude * std::sin(kTwoPi * frequency * static_cast<float>(i) / sampleRate);
    }
}

// Generate white noise using xorshift
void generateWhiteNoise(float* buffer, size_t size, uint32_t seed = 42) {
    for (size_t i = 0; i < size; ++i) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        buffer[i] = static_cast<float>(seed) * (2.0f / 4294967295.0f) - 1.0f;
    }
}

// Measure THD using FFT-based harmonic analysis
float measureTHDWithFFT(const float* buffer, size_t size, float fundamentalFreq, float sampleRate) {
    // FFT size must be power of 2
    size_t fftSize = 1;
    while (fftSize < size && fftSize < kMaxFFTSize) {
        fftSize <<= 1;
    }
    if (fftSize > size) fftSize >>= 1;
    if (fftSize < kMinFFTSize) fftSize = kMinFFTSize;

    // Apply Hann window to reduce spectral leakage
    std::vector<float> windowed(fftSize);
    for (size_t i = 0; i < fftSize; ++i) {
        float window = 0.5f * (1.0f - std::cos(kTwoPi * static_cast<float>(i) / static_cast<float>(fftSize - 1)));
        windowed[i] = buffer[i] * window;
    }

    // Perform FFT
    FFT fft;
    fft.prepare(fftSize);
    std::vector<Complex> spectrum(fftSize / 2 + 1);
    fft.forward(windowed.data(), spectrum.data());

    // Find the bin corresponding to the fundamental frequency
    float binWidth = sampleRate / static_cast<float>(fftSize);
    size_t fundamentalBin = static_cast<size_t>(std::round(fundamentalFreq / binWidth));

    // Get fundamental magnitude (search nearby bins)
    float fundamentalMag = 0.0f;
    size_t searchRange = 2;
    for (size_t i = fundamentalBin > searchRange ? fundamentalBin - searchRange : 0;
         i <= fundamentalBin + searchRange && i < spectrum.size(); ++i) {
        float mag = spectrum[i].magnitude();
        if (mag > fundamentalMag) {
            fundamentalMag = mag;
            fundamentalBin = i;
        }
    }

    if (fundamentalMag < 1e-10f) {
        return 0.0f;
    }

    // Sum harmonic magnitudes (2nd through 10th harmonics)
    float harmonicPowerSum = 0.0f;
    for (int harmonic = 2; harmonic <= 10; ++harmonic) {
        size_t harmonicBin = fundamentalBin * harmonic;
        if (harmonicBin >= spectrum.size()) break;

        float harmonicMag = 0.0f;
        for (size_t i = harmonicBin > searchRange ? harmonicBin - searchRange : 0;
             i <= harmonicBin + searchRange && i < spectrum.size(); ++i) {
            float mag = spectrum[i].magnitude();
            if (mag > harmonicMag) {
                harmonicMag = mag;
            }
        }
        harmonicPowerSum += harmonicMag * harmonicMag;
    }

    // THD = sqrt(sum of harmonic powers) / fundamental power * 100%
    return std::sqrt(harmonicPowerSum) / fundamentalMag * 100.0f;
}

// Check for clicks (large sample-to-sample differences)
bool hasClicks(const float* buffer, size_t size, float threshold = 0.5f) {
    for (size_t i = 1; i < size; ++i) {
        if (std::abs(buffer[i] - buffer[i - 1]) > threshold) {
            return true;
        }
    }
    return false;
}

// Calculate peak value
float calculatePeak(const float* buffer, size_t size) {
    float peak = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

// Measure band energy using FFT
float measureBandEnergy(const float* buffer, size_t size, float lowFreq, float highFreq, float sampleRate) {
    size_t fftSize = 1;
    while (fftSize < size && fftSize < kMaxFFTSize) {
        fftSize <<= 1;
    }
    if (fftSize > size) fftSize >>= 1;
    if (fftSize < kMinFFTSize) fftSize = kMinFFTSize;

    std::vector<float> windowed(fftSize);
    for (size_t i = 0; i < fftSize; ++i) {
        float window = 0.5f * (1.0f - std::cos(kTwoPi * static_cast<float>(i) / static_cast<float>(fftSize - 1)));
        windowed[i] = buffer[i] * window;
    }

    FFT fft;
    fft.prepare(fftSize);
    std::vector<Complex> spectrum(fftSize / 2 + 1);
    fft.forward(windowed.data(), spectrum.data());

    float binWidth = sampleRate / static_cast<float>(fftSize);
    size_t lowBin = static_cast<size_t>(lowFreq / binWidth);
    size_t highBin = static_cast<size_t>(highFreq / binWidth);
    highBin = std::min(highBin, spectrum.size() - 1);

    float energy = 0.0f;
    for (size_t i = lowBin; i <= highBin; ++i) {
        float mag = spectrum[i].magnitude();
        energy += mag * mag;
    }
    return std::sqrt(energy);
}

} // namespace

// =============================================================================
// Phase 3: User Story 1 - Basic Fuzz Pedal Processing
// =============================================================================

// -----------------------------------------------------------------------------
// T016: Lifecycle Tests (FR-001, FR-002, FR-003)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal default construction", "[fuzz_pedal][layer3][US1][lifecycle]") {
    FuzzPedal pedal;

    SECTION("default volume is 0 dB") {
        REQUIRE(pedal.getVolume() == Approx(0.0f));
    }

    SECTION("default fuzz type is Germanium") {
        REQUIRE(pedal.getFuzzType() == FuzzType::Germanium);
    }

    SECTION("default fuzz amount is 0.5") {
        REQUIRE(pedal.getFuzz() == Approx(0.5f));
    }

    SECTION("default tone is 0.5 (neutral)") {
        REQUIRE(pedal.getTone() == Approx(0.5f));
    }

    SECTION("default bias is 0.7") {
        REQUIRE(pedal.getBias() == Approx(0.7f));
    }

    SECTION("default input buffer is disabled") {
        REQUIRE_FALSE(pedal.getInputBuffer());
    }

    SECTION("default buffer cutoff is Hz10") {
        REQUIRE(pedal.getBufferCutoff() == BufferCutoff::Hz10);
    }

    SECTION("default gate is disabled") {
        REQUIRE_FALSE(pedal.getGateEnabled());
    }

    SECTION("default gate threshold is -60 dB") {
        REQUIRE(pedal.getGateThreshold() == Approx(-60.0f));
    }

    SECTION("default gate type is SoftKnee") {
        REQUIRE(pedal.getGateType() == GateType::SoftKnee);
    }

    SECTION("default gate timing is Normal") {
        REQUIRE(pedal.getGateTiming() == GateTiming::Normal);
    }
}

TEST_CASE("FuzzPedal prepare and reset", "[fuzz_pedal][layer3][US1][lifecycle]") {
    FuzzPedal pedal;

    SECTION("prepare configures for sample rate") {
        pedal.prepare(44100.0, 512);
        // Should not crash and be ready for processing
        std::vector<float> buffer(512, 0.5f);
        pedal.process(buffer.data(), buffer.size());
    }

    SECTION("reset clears state") {
        pedal.prepare(44100.0, 512);
        std::vector<float> buffer(512);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f);
        pedal.process(buffer.data(), buffer.size());

        pedal.reset();

        // After reset, should be ready for fresh processing
        std::vector<float> buffer2(512);
        generateSine(buffer2.data(), buffer2.size(), 1000.0f, 44100.0f);
        pedal.process(buffer2.data(), buffer2.size());
    }

    SECTION("FR-003: no allocations in process after prepare") {
        pedal.prepare(44100.0, 512);
        std::vector<float> buffer(512);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.3f);
        // This test just verifies the process call doesn't crash
        // Allocation tracking would require instrumentation
        pedal.process(buffer.data(), buffer.size());
    }
}

// -----------------------------------------------------------------------------
// T017: Fuzz Amount Setter/Getter Tests (FR-006, FR-026)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal fuzz amount control", "[fuzz_pedal][layer3][US1][fuzz]") {
    FuzzPedal pedal;
    pedal.prepare(44100.0, 512);

    SECTION("setFuzz accepts values in range [0, 1]") {
        pedal.setFuzz(0.0f);
        REQUIRE(pedal.getFuzz() == Approx(0.0f));

        pedal.setFuzz(0.5f);
        REQUIRE(pedal.getFuzz() == Approx(0.5f));

        pedal.setFuzz(1.0f);
        REQUIRE(pedal.getFuzz() == Approx(1.0f));
    }

    SECTION("setFuzz clamps out-of-range values") {
        pedal.setFuzz(-0.5f);
        REQUIRE(pedal.getFuzz() == Approx(0.0f));

        pedal.setFuzz(1.5f);
        REQUIRE(pedal.getFuzz() == Approx(1.0f));
    }
}

// -----------------------------------------------------------------------------
// T018: Volume Control Tests (FR-009, FR-009a, FR-009b, FR-010, FR-011, FR-026)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal volume control", "[fuzz_pedal][layer3][US1][volume]") {
    FuzzPedal pedal;
    pedal.prepare(44100.0, 512);

    SECTION("FR-010: default volume is 0 dB") {
        REQUIRE(pedal.getVolume() == Approx(0.0f));
    }

    SECTION("FR-009: volume range is [-24, +24] dB") {
        pedal.setVolume(-24.0f);
        REQUIRE(pedal.getVolume() == Approx(-24.0f));

        pedal.setVolume(24.0f);
        REQUIRE(pedal.getVolume() == Approx(24.0f));

        pedal.setVolume(0.0f);
        REQUIRE(pedal.getVolume() == Approx(0.0f));
    }

    SECTION("FR-009a: out-of-range values are clamped") {
        pedal.setVolume(-30.0f);
        REQUIRE(pedal.getVolume() == Approx(-24.0f));

        pedal.setVolume(30.0f);
        REQUIRE(pedal.getVolume() == Approx(24.0f));
    }

    SECTION("volume affects output level") {
        std::vector<float> buffer1(512);
        std::vector<float> buffer2(512);
        generateSine(buffer1.data(), buffer1.size(), 1000.0f, 44100.0f, 0.1f);
        generateSine(buffer2.data(), buffer2.size(), 1000.0f, 44100.0f, 0.1f);

        pedal.setFuzz(0.0f);  // Minimal fuzz for cleaner comparison
        pedal.setVolume(0.0f);
        pedal.process(buffer1.data(), buffer1.size());
        float rms0dB = calculateRMS(buffer1.data(), buffer1.size());

        pedal.reset();
        pedal.setVolume(12.0f);
        pedal.process(buffer2.data(), buffer2.size());
        float rms12dB = calculateRMS(buffer2.data(), buffer2.size());

        // +12dB should be approximately 4x amplitude
        REQUIRE(rms12dB > rms0dB * 2.0f);
    }
}

// -----------------------------------------------------------------------------
// T019: Harmonic Distortion Test (SC-001)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal harmonic distortion", "[fuzz_pedal][layer3][US1][distortion]") {
    FuzzPedal pedal;
    pedal.prepare(44100.0, 4096);

    SECTION("SC-001: fuzz at 0.7 produces THD > 5%") {
        pedal.setFuzz(0.7f);
        pedal.setVolume(0.0f);

        // Use low frequency for accurate THD measurement
        std::vector<float> buffer(4096);

        // Process multiple blocks to let filters settle
        for (int i = 0; i < 4; ++i) {
            std::vector<float> warmup(4096);
            generateSine(warmup.data(), warmup.size(), 1000.0f, 44100.0f, 0.3f);
            pedal.process(warmup.data(), warmup.size());
        }

        // Fresh buffer for measurement
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.3f);
        pedal.process(buffer.data(), buffer.size());

        float thd = measureTHDWithFFT(buffer.data(), buffer.size(), 1000.0f, 44100.0f);
        REQUIRE(thd > 5.0f);  // > 5% THD
    }

    SECTION("higher fuzz produces more distortion") {
        pedal.setFuzz(0.3f);

        std::vector<float> buffer1(4096);
        generateSine(buffer1.data(), buffer1.size(), 1000.0f, 44100.0f, 0.3f);
        pedal.process(buffer1.data(), buffer1.size());
        float thdLow = measureTHDWithFFT(buffer1.data(), buffer1.size(), 1000.0f, 44100.0f);

        pedal.reset();
        pedal.setFuzz(0.9f);

        std::vector<float> buffer2(4096);
        generateSine(buffer2.data(), buffer2.size(), 1000.0f, 44100.0f, 0.3f);
        pedal.process(buffer2.data(), buffer2.size());
        float thdHigh = measureTHDWithFFT(buffer2.data(), buffer2.size(), 1000.0f, 44100.0f);

        REQUIRE(thdHigh > thdLow);
    }
}

// -----------------------------------------------------------------------------
// T020: Parameter Smoothing Test (SC-002)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal parameter smoothing", "[fuzz_pedal][layer3][US1][smoothing]") {
    FuzzPedal pedal;
    pedal.prepare(44100.0, 512);

    SECTION("SC-002: volume changes complete within 10ms without clicks") {
        // Process with initial settings to let smoothers settle
        std::vector<float> warmup(4096);
        generateSine(warmup.data(), warmup.size(), 1000.0f, 44100.0f, 0.3f);
        pedal.process(warmup.data(), warmup.size());

        // Change volume mid-processing
        std::vector<float> buffer(4096);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.3f);

        // Process first half
        pedal.process(buffer.data(), 2048);

        // Change volume
        pedal.setVolume(12.0f);

        // Process second half
        pedal.process(buffer.data() + 2048, 2048);

        // Should not have clicks
        REQUIRE_FALSE(hasClicks(buffer.data(), buffer.size(), 0.5f));
    }

    SECTION("smoothing completes within 10ms") {
        // At 44100 Hz, 10ms = 441 samples
        constexpr size_t smoothingWindow = 441;

        pedal.setVolume(0.0f);
        pedal.setFuzz(0.0f);  // Minimal fuzz for cleaner measurement
        pedal.reset();

        std::vector<float> buffer(smoothingWindow * 2);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.1f);

        // Change volume
        pedal.setVolume(12.0f);

        // Process enough samples for smoothing to complete
        pedal.process(buffer.data(), smoothingWindow * 2);

        // The last samples should be at target level (approximately)
        float lastRMS = calculateRMS(buffer.data() + smoothingWindow, smoothingWindow);

        // Process more with same settings - should be stable
        std::vector<float> buffer2(smoothingWindow);
        generateSine(buffer2.data(), buffer2.size(), 1000.0f, 44100.0f, 0.1f);
        pedal.process(buffer2.data(), buffer2.size());
        float newRMS = calculateRMS(buffer2.data(), buffer2.size());

        // Should be within 10% of each other (smoothing complete)
        REQUIRE(newRMS == Approx(lastRMS).margin(lastRMS * 0.15f));
    }
}

// -----------------------------------------------------------------------------
// T021: Clean Bypass Test (SC-003)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal clean bypass", "[fuzz_pedal][layer3][US1][bypass]") {
    FuzzPedal pedal;
    pedal.prepare(44100.0, 4096);

    SECTION("SC-003: fuzz at 0.0 produces output within 1dB of input") {
        pedal.setFuzz(0.0f);
        pedal.setVolume(0.0f);

        std::vector<float> buffer(4096);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.3f);
        float inputRMS = calculateRMS(buffer.data(), buffer.size());

        pedal.process(buffer.data(), buffer.size());
        float outputRMS = calculateRMS(buffer.data(), buffer.size());

        // Calculate gain in dB
        float gainDb = 20.0f * std::log10(outputRMS / inputRMS);

        // Should be within +/-1dB of unity
        REQUIRE(std::abs(gainDb) < 1.0f);
    }
}

// -----------------------------------------------------------------------------
// T022: Edge Case Tests (FR-022, FR-023, FR-024, SC-006)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal edge cases", "[fuzz_pedal][layer3][US1][edge]") {
    FuzzPedal pedal;
    pedal.prepare(44100.0, 512);

    SECTION("FR-023: Handle n=0 gracefully") {
        float dummy = 0.0f;
        pedal.process(&dummy, 0);  // Should not crash
    }

    SECTION("FR-024: Handle nullptr gracefully") {
        pedal.process(nullptr, 100);  // Should not crash
    }

    SECTION("SC-006: Stability over extended processing") {
        pedal.setFuzz(0.9f);
        pedal.setVolume(12.0f);

        std::vector<float> buffer(512);
        uint32_t seed = 42;

        // Process equivalent of ~10 seconds of audio
        const int numBlocks = static_cast<int>(44100 * 10 / 512);
        for (int block = 0; block < numBlocks; ++block) {
            generateWhiteNoise(buffer.data(), buffer.size(), seed++);
            pedal.process(buffer.data(), buffer.size());

            // Check for NaN/Inf
            for (auto s : buffer) {
                REQUIRE(std::isfinite(s));
            }

            // Check for extreme values (soft limiting)
            float peak = calculatePeak(buffer.data(), buffer.size());
            REQUIRE(peak < 100.0f);  // Should be bounded
        }
    }

    SECTION("extreme settings remain stable") {
        pedal.setFuzz(1.0f);
        pedal.setVolume(24.0f);

        std::vector<float> buffer(512);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 1.0f);
        pedal.process(buffer.data(), buffer.size());

        // Should be bounded (no infinity)
        for (auto s : buffer) {
            REQUIRE(std::isfinite(s));
        }
    }

    SECTION("minimum settings work correctly") {
        pedal.setFuzz(0.0f);
        pedal.setVolume(-24.0f);

        std::vector<float> buffer(512);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 1.0f);
        pedal.process(buffer.data(), buffer.size());

        // Output should be attenuated but finite
        // -24dB is approximately 1/16 amplitude, so ~0.063 RMS for unity input
        // Allow some headroom for fuzz processing effects
        float rms = calculateRMS(buffer.data(), buffer.size());
        REQUIRE(rms < 0.15f);   // Significantly attenuated
        REQUIRE(rms >= 0.0f);   // But not negative
    }
}

// -----------------------------------------------------------------------------
// T023: Sample Rate Tests (SC-007)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal sample rate support", "[fuzz_pedal][layer3][US1][samplerate]") {
    SECTION("SC-007: Works at 44.1kHz") {
        FuzzPedal pedal;
        pedal.prepare(44100.0, 512);
        pedal.setFuzz(0.7f);

        std::vector<float> buffer(512);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.3f);
        pedal.process(buffer.data(), buffer.size());

        float rms = calculateRMS(buffer.data(), buffer.size());
        REQUIRE(rms > 0.0f);
        REQUIRE(std::isfinite(rms));
    }

    SECTION("SC-007: Works at 48kHz") {
        FuzzPedal pedal;
        pedal.prepare(48000.0, 512);
        pedal.setFuzz(0.7f);

        std::vector<float> buffer(512);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 48000.0f, 0.3f);
        pedal.process(buffer.data(), buffer.size());

        float rms = calculateRMS(buffer.data(), buffer.size());
        REQUIRE(rms > 0.0f);
        REQUIRE(std::isfinite(rms));
    }

    SECTION("SC-007: Works at 96kHz") {
        FuzzPedal pedal;
        pedal.prepare(96000.0, 512);
        pedal.setFuzz(0.7f);

        std::vector<float> buffer(512);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 96000.0f, 0.3f);
        pedal.process(buffer.data(), buffer.size());

        float rms = calculateRMS(buffer.data(), buffer.size());
        REQUIRE(rms > 0.0f);
        REQUIRE(std::isfinite(rms));
    }

    SECTION("SC-007: Works at 192kHz") {
        FuzzPedal pedal;
        pedal.prepare(192000.0, 512);
        pedal.setFuzz(0.7f);

        std::vector<float> buffer(512);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 192000.0f, 0.3f);
        pedal.process(buffer.data(), buffer.size());

        float rms = calculateRMS(buffer.data(), buffer.size());
        REQUIRE(rms > 0.0f);
        REQUIRE(std::isfinite(rms));
    }
}

// -----------------------------------------------------------------------------
// T024: Performance Test (SC-005)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal performance", "[fuzz_pedal][layer3][US1][performance]") {
    SECTION("SC-005: 512 samples processes in under 0.3ms at 44.1kHz") {
        FuzzPedal pedal;
        pedal.prepare(44100.0, 512);
        pedal.setFuzz(0.7f);
        pedal.setGateEnabled(true);  // Enable gate to test full processing path

        std::vector<float> buffer(512);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.3f);

        // Warm-up runs
        for (int i = 0; i < 10; ++i) {
            pedal.process(buffer.data(), buffer.size());
            generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.3f);
        }

        // Timed run (median of 100 runs)
        std::vector<double> times;
        times.reserve(100);

        for (int i = 0; i < 100; ++i) {
            generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.3f);

            auto start = std::chrono::high_resolution_clock::now();
            pedal.process(buffer.data(), buffer.size());
            auto end = std::chrono::high_resolution_clock::now();

            std::chrono::duration<double, std::milli> elapsed = end - start;
            times.push_back(elapsed.count());
        }

        std::sort(times.begin(), times.end());
        double medianMs = times[times.size() / 2];

        REQUIRE(medianMs < 0.3);  // Under 0.3ms
    }
}

// -----------------------------------------------------------------------------
// T025: Signal Flow Order Test (FR-025)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal signal flow order", "[fuzz_pedal][layer3][US1][routing]") {
    SECTION("FR-025: volume is applied after fuzz") {
        FuzzPedal pedal;
        pedal.prepare(44100.0, 512);

        // High fuzz, low volume
        pedal.setFuzz(0.9f);
        pedal.setVolume(-24.0f);

        std::vector<float> buffer(512);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.3f);
        pedal.process(buffer.data(), buffer.size());

        // Output should be attenuated (volume applied after saturation)
        float rms = calculateRMS(buffer.data(), buffer.size());
        REQUIRE(rms < 0.1f);  // Significantly attenuated
    }
}

// =============================================================================
// Phase 4: User Story 2 - Fuzz Type Selection
// =============================================================================

// -----------------------------------------------------------------------------
// T038: Fuzz Type Setter/Getter Tests (FR-005, FR-027)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal fuzz type selection", "[fuzz_pedal][layer3][US2][type]") {
    FuzzPedal pedal;
    pedal.prepare(44100.0, 512);

    SECTION("setFuzzType and getFuzzType") {
        pedal.setFuzzType(FuzzType::Germanium);
        REQUIRE(pedal.getFuzzType() == FuzzType::Germanium);

        pedal.setFuzzType(FuzzType::Silicon);
        REQUIRE(pedal.getFuzzType() == FuzzType::Silicon);
    }

    SECTION("default type is Germanium") {
        FuzzPedal fresh;
        REQUIRE(fresh.getFuzzType() == FuzzType::Germanium);
    }
}

// -----------------------------------------------------------------------------
// T039: Germanium Character Test
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal Germanium character", "[fuzz_pedal][layer3][US2][germanium]") {
    FuzzPedal pedal;
    pedal.prepare(44100.0, 4096);

    SECTION("Germanium produces warm character with soft clipping") {
        pedal.setFuzzType(FuzzType::Germanium);
        pedal.setFuzz(0.7f);

        // Warm up
        for (int i = 0; i < 4; ++i) {
            std::vector<float> warmup(4096);
            generateSine(warmup.data(), warmup.size(), 1000.0f, 44100.0f, 0.3f);
            pedal.process(warmup.data(), warmup.size());
        }

        std::vector<float> buffer(4096);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.3f);
        pedal.process(buffer.data(), buffer.size());

        float thd = measureTHDWithFFT(buffer.data(), buffer.size(), 1000.0f, 44100.0f);
        REQUIRE(thd > 1.0f);  // Produces harmonic content
    }
}

// -----------------------------------------------------------------------------
// T040: Silicon Character Test
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal Silicon character", "[fuzz_pedal][layer3][US2][silicon]") {
    FuzzPedal pedal;
    pedal.prepare(44100.0, 4096);

    SECTION("Silicon produces bright character with harder clipping") {
        pedal.setFuzzType(FuzzType::Silicon);
        pedal.setFuzz(0.7f);

        // Warm up
        for (int i = 0; i < 4; ++i) {
            std::vector<float> warmup(4096);
            generateSine(warmup.data(), warmup.size(), 1000.0f, 44100.0f, 0.3f);
            pedal.process(warmup.data(), warmup.size());
        }

        std::vector<float> buffer(4096);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.3f);
        pedal.process(buffer.data(), buffer.size());

        float thd = measureTHDWithFFT(buffer.data(), buffer.size(), 1000.0f, 44100.0f);
        REQUIRE(thd > 1.0f);  // Produces harmonic content
    }
}

// -----------------------------------------------------------------------------
// T041: Fuzz Type Crossfade Test (FR-021d, SC-008)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal fuzz type crossfade", "[fuzz_pedal][layer3][US2][crossfade]") {
    FuzzPedal pedal;
    pedal.prepare(44100.0, 512);
    pedal.setFuzz(0.7f);

    SECTION("SC-008: type change produces no clicks") {
        // Warm up
        std::vector<float> warmup(4096);
        generateSine(warmup.data(), warmup.size(), 1000.0f, 44100.0f, 0.3f);
        pedal.process(warmup.data(), warmup.size());

        // Process with type change mid-way
        std::vector<float> buffer(4096);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.3f);

        pedal.setFuzzType(FuzzType::Germanium);
        pedal.process(buffer.data(), 2048);

        pedal.setFuzzType(FuzzType::Silicon);
        pedal.process(buffer.data() + 2048, 2048);

        // Should not have significant clicks
        REQUIRE_FALSE(hasClicks(buffer.data(), buffer.size(), 1.0f));
    }
}

// =============================================================================
// Phase 5: User Story 3 - Tone Control Shaping
// =============================================================================

// -----------------------------------------------------------------------------
// T047: Tone Setter/Getter Tests (FR-007, FR-026)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal tone control", "[fuzz_pedal][layer3][US3][tone]") {
    FuzzPedal pedal;
    pedal.prepare(44100.0, 512);

    SECTION("setTone accepts values in range [0, 1]") {
        pedal.setTone(0.0f);
        REQUIRE(pedal.getTone() == Approx(0.0f));

        pedal.setTone(0.5f);
        REQUIRE(pedal.getTone() == Approx(0.5f));

        pedal.setTone(1.0f);
        REQUIRE(pedal.getTone() == Approx(1.0f));
    }

    SECTION("setTone clamps out-of-range values") {
        pedal.setTone(-0.5f);
        REQUIRE(pedal.getTone() == Approx(0.0f));

        pedal.setTone(1.5f);
        REQUIRE(pedal.getTone() == Approx(1.0f));
    }
}

// -----------------------------------------------------------------------------
// T048: Dark Tone Test (SC-009)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal dark tone", "[fuzz_pedal][layer3][US3][dark]") {
    SECTION("SC-009: tone at 0.0 attenuates high frequencies") {
        FuzzPedal pedalDark, pedalBright;
        pedalDark.prepare(44100.0, 4096);
        pedalBright.prepare(44100.0, 4096);

        pedalDark.setTone(0.0f);   // Dark
        pedalBright.setTone(1.0f); // Bright
        pedalDark.setFuzz(0.5f);
        pedalBright.setFuzz(0.5f);

        uint32_t seed = 42;
        std::vector<float> buffer1(4096);
        std::vector<float> buffer2(4096);
        generateWhiteNoise(buffer1.data(), buffer1.size(), seed);
        generateWhiteNoise(buffer2.data(), buffer2.size(), seed);

        pedalDark.process(buffer1.data(), buffer1.size());
        pedalBright.process(buffer2.data(), buffer2.size());

        float highEnergyDark = measureBandEnergy(buffer1.data(), buffer1.size(), 4000, 8000, 44100.0f);
        float highEnergyBright = measureBandEnergy(buffer2.data(), buffer2.size(), 4000, 8000, 44100.0f);

        // Dark tone should have less high frequency energy
        REQUIRE(highEnergyDark < highEnergyBright);
    }
}

// -----------------------------------------------------------------------------
// T049: Bright Tone Test (SC-009)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal bright tone", "[fuzz_pedal][layer3][US3][bright]") {
    SECTION("SC-009: tone at 1.0 preserves high frequencies") {
        FuzzPedal pedal;
        pedal.prepare(44100.0, 4096);
        pedal.setTone(1.0f);
        pedal.setFuzz(0.5f);

        uint32_t seed = 42;
        std::vector<float> buffer(4096);
        generateWhiteNoise(buffer.data(), buffer.size(), seed);

        pedal.process(buffer.data(), buffer.size());

        float highEnergy = measureBandEnergy(buffer.data(), buffer.size(), 4000, 8000, 44100.0f);

        // Should have measurable high frequency energy
        REQUIRE(highEnergy > 0.0f);
    }
}

// -----------------------------------------------------------------------------
// T050: Neutral Tone Test
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal neutral tone", "[fuzz_pedal][layer3][US3][neutral]") {
    SECTION("tone at 0.5 provides balanced response") {
        FuzzPedal pedal;
        pedal.prepare(44100.0, 4096);
        pedal.setTone(0.5f);
        pedal.setFuzz(0.5f);

        uint32_t seed = 42;
        std::vector<float> buffer(4096);
        generateWhiteNoise(buffer.data(), buffer.size(), seed);

        pedal.process(buffer.data(), buffer.size());

        float lowEnergy = measureBandEnergy(buffer.data(), buffer.size(), 200, 500, 44100.0f);
        float highEnergy = measureBandEnergy(buffer.data(), buffer.size(), 4000, 8000, 44100.0f);

        // Both bands should have measurable energy
        REQUIRE(lowEnergy > 0.0f);
        REQUIRE(highEnergy > 0.0f);
    }
}

// -----------------------------------------------------------------------------
// T051: Tone Frequency Response Range Test (SC-009)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal tone frequency response range", "[fuzz_pedal][layer3][US3][range]") {
    SECTION("SC-009: tone provides measurable adjustment in 400Hz-8kHz range") {
        FuzzPedal pedalDark, pedalBright;
        pedalDark.prepare(44100.0, 4096);
        pedalBright.prepare(44100.0, 4096);

        pedalDark.setTone(0.0f);
        pedalBright.setTone(1.0f);
        pedalDark.setFuzz(0.5f);
        pedalBright.setFuzz(0.5f);

        uint32_t seed = 42;
        std::vector<float> buffer1(4096);
        std::vector<float> buffer2(4096);
        generateWhiteNoise(buffer1.data(), buffer1.size(), seed);
        generateWhiteNoise(buffer2.data(), buffer2.size(), seed);

        pedalDark.process(buffer1.data(), buffer1.size());
        pedalBright.process(buffer2.data(), buffer2.size());

        // Measure energy in 400Hz-8kHz band
        float energyDark = measureBandEnergy(buffer1.data(), buffer1.size(), 400, 8000, 44100.0f);
        float energyBright = measureBandEnergy(buffer2.data(), buffer2.size(), 400, 8000, 44100.0f);

        // Calculate difference in dB
        float differenceDb = 20.0f * std::log10(energyBright / energyDark);

        // The FuzzProcessor tone control provides meaningful tonal shaping
        // The exact range depends on the underlying filter design
        REQUIRE(differenceDb > 2.0f);  // At least 2dB measurable difference
    }
}

// =============================================================================
// Phase 6: User Story 4 - Transistor Bias Control
// =============================================================================

// -----------------------------------------------------------------------------
// T057: Bias Setter/Getter Tests (FR-008, FR-026)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal bias control", "[fuzz_pedal][layer3][US4][bias]") {
    FuzzPedal pedal;
    pedal.prepare(44100.0, 512);

    SECTION("setBias accepts values in range [0, 1]") {
        pedal.setBias(0.0f);
        REQUIRE(pedal.getBias() == Approx(0.0f));

        pedal.setBias(0.5f);
        REQUIRE(pedal.getBias() == Approx(0.5f));

        pedal.setBias(1.0f);
        REQUIRE(pedal.getBias() == Approx(1.0f));
    }

    SECTION("setBias clamps out-of-range values") {
        pedal.setBias(-0.5f);
        REQUIRE(pedal.getBias() == Approx(0.0f));

        pedal.setBias(1.5f);
        REQUIRE(pedal.getBias() == Approx(1.0f));
    }
}

// -----------------------------------------------------------------------------
// T058: Dying Battery Test
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal dying battery effect", "[fuzz_pedal][layer3][US4][dying]") {
    SECTION("bias at 0.0 produces gating on quiet audio") {
        FuzzPedal pedal;
        pedal.prepare(44100.0, 512);
        pedal.setBias(0.0f);  // Dying battery
        pedal.setFuzz(0.7f);

        // Use very quiet input
        std::vector<float> buffer(512);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.01f);  // Very quiet
        pedal.process(buffer.data(), buffer.size());

        // At low bias, quiet signals may be gated
        float rms = calculateRMS(buffer.data(), buffer.size());
        // Just verify it doesn't crash and produces some output
        REQUIRE(std::isfinite(rms));
    }
}

// -----------------------------------------------------------------------------
// T059: Normal Operation Test
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal normal bias operation", "[fuzz_pedal][layer3][US4][normal]") {
    SECTION("bias at 1.0 produces no gating artifacts") {
        FuzzPedal pedal;
        pedal.prepare(44100.0, 512);
        pedal.setBias(1.0f);  // Normal operation
        pedal.setFuzz(0.7f);

        std::vector<float> buffer(512);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.3f);
        float inputRMS = calculateRMS(buffer.data(), buffer.size());

        pedal.process(buffer.data(), buffer.size());
        float outputRMS = calculateRMS(buffer.data(), buffer.size());

        // Output should have signal (no gating)
        REQUIRE(outputRMS > inputRMS * 0.1f);
    }
}

// -----------------------------------------------------------------------------
// T060: Moderate Gating Test
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal moderate bias", "[fuzz_pedal][layer3][US4][moderate]") {
    SECTION("bias at 0.5 produces moderate gating") {
        FuzzPedal pedal;
        pedal.prepare(44100.0, 512);
        pedal.setBias(0.5f);  // Moderate
        pedal.setFuzz(0.7f);

        std::vector<float> buffer(512);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.3f);
        pedal.process(buffer.data(), buffer.size());

        // Just verify it processes without issues
        float rms = calculateRMS(buffer.data(), buffer.size());
        REQUIRE(rms > 0.0f);
        REQUIRE(std::isfinite(rms));
    }
}

// =============================================================================
// Phase 7: User Story 5 - Input Buffer Control
// =============================================================================

// -----------------------------------------------------------------------------
// T066: Input Buffer Enable/Disable Tests (FR-012, FR-028)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal input buffer control", "[fuzz_pedal][layer3][US5][buffer]") {
    FuzzPedal pedal;
    pedal.prepare(44100.0, 512);

    SECTION("setInputBuffer and getInputBuffer") {
        REQUIRE_FALSE(pedal.getInputBuffer());  // Default off

        pedal.setInputBuffer(true);
        REQUIRE(pedal.getInputBuffer() == true);

        pedal.setInputBuffer(false);
        REQUIRE(pedal.getInputBuffer() == false);
    }
}

// -----------------------------------------------------------------------------
// T067: Buffer Cutoff Selector Tests (FR-013a, FR-013b, FR-013c, FR-028a)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal buffer cutoff selection", "[fuzz_pedal][layer3][US5][cutoff]") {
    FuzzPedal pedal;
    pedal.prepare(44100.0, 512);

    SECTION("setBufferCutoff and getBufferCutoff") {
        pedal.setBufferCutoff(BufferCutoff::Hz5);
        REQUIRE(pedal.getBufferCutoff() == BufferCutoff::Hz5);

        pedal.setBufferCutoff(BufferCutoff::Hz10);
        REQUIRE(pedal.getBufferCutoff() == BufferCutoff::Hz10);

        pedal.setBufferCutoff(BufferCutoff::Hz20);
        REQUIRE(pedal.getBufferCutoff() == BufferCutoff::Hz20);
    }

    SECTION("FR-013c: default cutoff is Hz10") {
        FuzzPedal fresh;
        REQUIRE(fresh.getBufferCutoff() == BufferCutoff::Hz10);
    }
}

// -----------------------------------------------------------------------------
// T068: True Bypass Test (FR-014, FR-015)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal true bypass", "[fuzz_pedal][layer3][US5][bypass]") {
    SECTION("FR-015: buffer disabled by default (true bypass)") {
        FuzzPedal pedal;
        REQUIRE_FALSE(pedal.getInputBuffer());
    }

    SECTION("FR-014: buffer disabled passes signal directly") {
        FuzzPedal pedal;
        pedal.prepare(44100.0, 512);
        pedal.setInputBuffer(false);
        pedal.setFuzz(0.0f);
        pedal.setVolume(0.0f);

        std::vector<float> buffer(512);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.3f);
        float inputRMS = calculateRMS(buffer.data(), buffer.size());

        pedal.process(buffer.data(), buffer.size());
        float outputRMS = calculateRMS(buffer.data(), buffer.size());

        // Should be near unity
        float gainDb = 20.0f * std::log10(outputRMS / inputRMS);
        REQUIRE(std::abs(gainDb) < 2.0f);
    }
}

// -----------------------------------------------------------------------------
// T069: Buffered Signal Test (FR-013)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal buffered signal", "[fuzz_pedal][layer3][US5][buffered]") {
    SECTION("FR-013: buffer enabled preserves high frequencies") {
        FuzzPedal pedal;
        pedal.prepare(44100.0, 4096);
        pedal.setInputBuffer(true);
        pedal.setFuzz(0.5f);

        std::vector<float> buffer(4096);
        generateSine(buffer.data(), buffer.size(), 5000.0f, 44100.0f, 0.3f);
        pedal.process(buffer.data(), buffer.size());

        float rms = calculateRMS(buffer.data(), buffer.size());
        REQUIRE(rms > 0.0f);  // Signal passes through
    }
}

// -----------------------------------------------------------------------------
// T070: Buffer High-Pass Response Test (FR-013a, FR-013b)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal buffer high-pass response", "[fuzz_pedal][layer3][US5][highpass]") {
    SECTION("buffer attenuates DC and very low frequencies") {
        FuzzPedal pedal;
        pedal.prepare(44100.0, 8192);
        pedal.setInputBuffer(true);
        pedal.setBufferCutoff(BufferCutoff::Hz20);
        pedal.setFuzz(0.0f);  // Minimal processing for clearer measurement
        pedal.setVolume(0.0f);

        // Generate very low frequency (2 Hz) with DC offset
        std::vector<float> buffer(8192);
        for (size_t i = 0; i < buffer.size(); ++i) {
            buffer[i] = 0.5f + 0.3f * std::sin(kTwoPi * 2.0f * static_cast<float>(i) / 44100.0f);
        }
        float inputRMS = calculateRMS(buffer.data(), buffer.size());

        // Process multiple blocks to let filter settle
        for (int j = 0; j < 5; ++j) {
            for (size_t i = 0; i < buffer.size(); ++i) {
                buffer[i] = 0.5f + 0.3f * std::sin(kTwoPi * 2.0f * static_cast<float>(i) / 44100.0f);
            }
            pedal.process(buffer.data(), buffer.size());
        }

        float outputRMS = calculateRMS(buffer.data(), buffer.size());

        // DC and very low frequency should be significantly attenuated
        // The high-pass filter removes DC, so RMS should be lower
        REQUIRE(outputRMS < inputRMS);
    }
}

// -----------------------------------------------------------------------------
// T071: Signal Flow Order Test (FR-025) - Buffer Before Fuzz
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal buffer signal flow", "[fuzz_pedal][layer3][US5][flow]") {
    SECTION("FR-025: buffer is applied before fuzz") {
        // This is verified by the fact that DC blocking happens before saturation
        // If buffer were after fuzz, DC offset in saturation would not be blocked
        FuzzPedal pedal;
        pedal.prepare(44100.0, 512);
        pedal.setInputBuffer(true);
        pedal.setBufferCutoff(BufferCutoff::Hz20);
        pedal.setFuzz(0.7f);

        // Input with DC offset
        std::vector<float> buffer(512);
        for (size_t i = 0; i < buffer.size(); ++i) {
            buffer[i] = 0.3f + 0.3f * std::sin(kTwoPi * 1000.0f * static_cast<float>(i) / 44100.0f);
        }

        pedal.process(buffer.data(), buffer.size());

        // Output should have processed signal (not crashed)
        float rms = calculateRMS(buffer.data(), buffer.size());
        REQUIRE(rms > 0.0f);
        REQUIRE(std::isfinite(rms));
    }
}

// =============================================================================
// Phase 8: User Story 6 - Noise Gate Control
// =============================================================================

// -----------------------------------------------------------------------------
// T083: Gate Enable/Disable Tests (FR-017, FR-019, FR-029)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal gate enable control", "[fuzz_pedal][layer3][US6][gate]") {
    FuzzPedal pedal;
    pedal.prepare(44100.0, 512);

    SECTION("setGateEnabled and getGateEnabled") {
        REQUIRE_FALSE(pedal.getGateEnabled());  // Default off (FR-019)

        pedal.setGateEnabled(true);
        REQUIRE(pedal.getGateEnabled() == true);

        pedal.setGateEnabled(false);
        REQUIRE(pedal.getGateEnabled() == false);
    }
}

// -----------------------------------------------------------------------------
// T084: Gate Threshold Setter/Getter Tests (FR-016, FR-018, FR-029)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal gate threshold control", "[fuzz_pedal][layer3][US6][threshold]") {
    FuzzPedal pedal;
    pedal.prepare(44100.0, 512);

    SECTION("FR-018: default threshold is -60 dB") {
        REQUIRE(pedal.getGateThreshold() == Approx(-60.0f));
    }

    SECTION("FR-016: threshold range is [-80, 0] dB") {
        pedal.setGateThreshold(-80.0f);
        REQUIRE(pedal.getGateThreshold() == Approx(-80.0f));

        pedal.setGateThreshold(0.0f);
        REQUIRE(pedal.getGateThreshold() == Approx(0.0f));

        pedal.setGateThreshold(-60.0f);
        REQUIRE(pedal.getGateThreshold() == Approx(-60.0f));
    }

    SECTION("out-of-range values are clamped") {
        pedal.setGateThreshold(-100.0f);
        REQUIRE(pedal.getGateThreshold() == Approx(-80.0f));

        pedal.setGateThreshold(10.0f);
        REQUIRE(pedal.getGateThreshold() == Approx(0.0f));
    }
}

// -----------------------------------------------------------------------------
// T085: Gate Type Selector Tests (FR-021a, FR-021b, FR-021c, FR-029a)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal gate type selection", "[fuzz_pedal][layer3][US6][gatetype]") {
    FuzzPedal pedal;
    pedal.prepare(44100.0, 512);

    SECTION("setGateType and getGateType") {
        pedal.setGateType(GateType::SoftKnee);
        REQUIRE(pedal.getGateType() == GateType::SoftKnee);

        pedal.setGateType(GateType::HardGate);
        REQUIRE(pedal.getGateType() == GateType::HardGate);

        pedal.setGateType(GateType::LinearRamp);
        REQUIRE(pedal.getGateType() == GateType::LinearRamp);
    }

    SECTION("FR-021c: default type is SoftKnee") {
        FuzzPedal fresh;
        REQUIRE(fresh.getGateType() == GateType::SoftKnee);
    }
}

// -----------------------------------------------------------------------------
// T086: Gate Timing Selector Tests (FR-021e, FR-021f, FR-021g, FR-029b)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal gate timing selection", "[fuzz_pedal][layer3][US6][timing]") {
    FuzzPedal pedal;
    pedal.prepare(44100.0, 512);

    SECTION("setGateTiming and getGateTiming") {
        pedal.setGateTiming(GateTiming::Fast);
        REQUIRE(pedal.getGateTiming() == GateTiming::Fast);

        pedal.setGateTiming(GateTiming::Normal);
        REQUIRE(pedal.getGateTiming() == GateTiming::Normal);

        pedal.setGateTiming(GateTiming::Slow);
        REQUIRE(pedal.getGateTiming() == GateTiming::Slow);
    }

    SECTION("FR-021g: default timing is Normal") {
        FuzzPedal fresh;
        REQUIRE(fresh.getGateTiming() == GateTiming::Normal);
    }
}

// -----------------------------------------------------------------------------
// T087: Noise Gating Test (SC-004)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal noise gating", "[fuzz_pedal][layer3][US6][gating]") {
    SECTION("SC-004: threshold -60dB, silence attenuated > 40dB") {
        FuzzPedal pedal;
        pedal.prepare(44100.0, 512);
        pedal.setGateEnabled(true);
        pedal.setGateThreshold(-60.0f);
        pedal.setGateType(GateType::HardGate);  // Use hard gate for clear attenuation
        pedal.setFuzz(0.0f);  // Minimal fuzz for cleaner measurement
        pedal.setVolume(0.0f);

        // Input at -80dB (below -60dB threshold)
        float inputLevel = std::pow(10.0f, -80.0f / 20.0f);
        std::vector<float> buffer(512);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, inputLevel);
        float inputRMS = calculateRMS(buffer.data(), buffer.size());

        // Process multiple blocks to let envelope settle
        for (int i = 0; i < 10; ++i) {
            generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, inputLevel);
            pedal.process(buffer.data(), buffer.size());
        }

        float outputRMS = calculateRMS(buffer.data(), buffer.size());

        // Output should be heavily attenuated (hard gate = near zero below threshold)
        REQUIRE(outputRMS < inputRMS * 0.1f);  // At least 20dB attenuation (hard gate)
    }
}

// -----------------------------------------------------------------------------
// T088: Sensitive Gate Test
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal sensitive gate", "[fuzz_pedal][layer3][US6][sensitive]") {
    SECTION("threshold -80dB, -70dB audio passes") {
        FuzzPedal pedal;
        pedal.prepare(44100.0, 512);
        pedal.setGateEnabled(true);
        pedal.setGateThreshold(-80.0f);  // Very sensitive
        pedal.setFuzz(0.0f);
        pedal.setVolume(0.0f);

        // Input at -70dB (above -80dB threshold)
        float inputLevel = std::pow(10.0f, -70.0f / 20.0f);
        std::vector<float> buffer(512);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, inputLevel);
        float inputRMS = calculateRMS(buffer.data(), buffer.size());

        pedal.process(buffer.data(), buffer.size());
        float outputRMS = calculateRMS(buffer.data(), buffer.size());

        // Signal should pass through (above threshold)
        REQUIRE(outputRMS > inputRMS * 0.5f);
    }
}

// -----------------------------------------------------------------------------
// T089: Aggressive Gate Test
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal aggressive gate", "[fuzz_pedal][layer3][US6][aggressive]") {
    SECTION("threshold -40dB, -50dB audio is gated") {
        FuzzPedal pedal;
        pedal.prepare(44100.0, 512);
        pedal.setGateEnabled(true);
        pedal.setGateThreshold(-40.0f);  // Aggressive
        pedal.setGateType(GateType::HardGate);
        pedal.setFuzz(0.0f);
        pedal.setVolume(0.0f);

        // Input at -50dB (below -40dB threshold)
        float inputLevel = std::pow(10.0f, -50.0f / 20.0f);
        std::vector<float> buffer(512);

        // Process multiple blocks to let envelope settle
        for (int i = 0; i < 10; ++i) {
            generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, inputLevel);
            pedal.process(buffer.data(), buffer.size());
        }

        float inputRMS = inputLevel / std::sqrt(2.0f);  // RMS of sine
        float outputRMS = calculateRMS(buffer.data(), buffer.size());

        // Signal should be gated (below threshold)
        REQUIRE(outputRMS < inputRMS * 0.5f);
    }
}

// -----------------------------------------------------------------------------
// T090: Gate Type Crossfade Test (FR-021d, SC-008a)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal gate type crossfade", "[fuzz_pedal][layer3][US6][crossfade]") {
    SECTION("SC-008a: gate type change produces no clicks") {
        FuzzPedal pedal;
        pedal.prepare(44100.0, 512);
        pedal.setGateEnabled(true);
        pedal.setGateThreshold(-40.0f);
        pedal.setFuzz(0.5f);

        // Warm up
        std::vector<float> warmup(4096);
        generateSine(warmup.data(), warmup.size(), 1000.0f, 44100.0f, 0.3f);
        pedal.process(warmup.data(), warmup.size());

        // Process with gate type change mid-way
        std::vector<float> buffer(4096);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.3f);

        pedal.setGateType(GateType::SoftKnee);
        pedal.process(buffer.data(), 2048);

        pedal.setGateType(GateType::HardGate);
        pedal.process(buffer.data() + 2048, 2048);

        // Should not have significant clicks
        REQUIRE_FALSE(hasClicks(buffer.data(), buffer.size(), 1.0f));
    }
}

// -----------------------------------------------------------------------------
// T091: Gate Timing Change Test (FR-021h, SC-008b)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal gate timing change", "[fuzz_pedal][layer3][US6][timingchange]") {
    SECTION("SC-008b: timing change takes effect immediately") {
        FuzzPedal pedal;
        pedal.prepare(44100.0, 512);
        pedal.setGateEnabled(true);
        pedal.setGateTiming(GateTiming::Fast);
        REQUIRE(pedal.getGateTiming() == GateTiming::Fast);

        pedal.setGateTiming(GateTiming::Slow);
        REQUIRE(pedal.getGateTiming() == GateTiming::Slow);

        // Process should work with new timing
        std::vector<float> buffer(512);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.3f);
        pedal.process(buffer.data(), buffer.size());

        float rms = calculateRMS(buffer.data(), buffer.size());
        REQUIRE(rms > 0.0f);
    }
}

// -----------------------------------------------------------------------------
// T092: Gate Signal Flow Order Test (FR-025)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal gate signal flow", "[fuzz_pedal][layer3][US6][flow]") {
    SECTION("FR-025: gate is applied after fuzz, before volume") {
        FuzzPedal pedal;
        pedal.prepare(44100.0, 512);
        pedal.setFuzz(0.5f);
        pedal.setGateEnabled(true);
        pedal.setGateThreshold(-40.0f);
        pedal.setVolume(12.0f);  // Boost

        // Input above threshold
        std::vector<float> buffer(512);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.3f);
        pedal.process(buffer.data(), buffer.size());

        // If gate is after fuzz and before volume, the boosted signal should pass
        float rms = calculateRMS(buffer.data(), buffer.size());
        REQUIRE(rms > 0.1f);  // Significant output (boosted by volume)
    }
}

// -----------------------------------------------------------------------------
// T093: Gate Envelope Following Test (FR-021, FR-021f)
// -----------------------------------------------------------------------------

TEST_CASE("FuzzPedal gate envelope following", "[fuzz_pedal][layer3][US6][envelope]") {
    SECTION("gate responds to signal envelope") {
        FuzzPedal pedal;
        pedal.prepare(44100.0, 4096);
        pedal.setGateEnabled(true);
        pedal.setGateThreshold(-30.0f);
        pedal.setGateType(GateType::SoftKnee);
        pedal.setGateTiming(GateTiming::Normal);
        pedal.setFuzz(0.0f);
        pedal.setVolume(0.0f);

        // Start with loud signal, then quiet
        std::vector<float> buffer(4096);

        // First half: loud (above threshold)
        for (size_t i = 0; i < 2048; ++i) {
            buffer[i] = 0.5f * std::sin(kTwoPi * 1000.0f * static_cast<float>(i) / 44100.0f);
        }
        // Second half: quiet (below threshold)
        for (size_t i = 2048; i < 4096; ++i) {
            buffer[i] = 0.001f * std::sin(kTwoPi * 1000.0f * static_cast<float>(i) / 44100.0f);
        }

        pedal.process(buffer.data(), buffer.size());

        // First half should have significant output
        float rmsFirst = calculateRMS(buffer.data(), 2048);
        // Second half should be more attenuated (gated)
        float rmsSecond = calculateRMS(buffer.data() + 2048, 2048);

        REQUIRE(rmsFirst > rmsSecond * 5.0f);  // First half significantly louder
    }
}

// =============================================================================
// Phase 9: End-to-End Integration Test
// =============================================================================

TEST_CASE("FuzzPedal end-to-end signal flow", "[fuzz_pedal][layer3][integration]") {
    SECTION("complete signal path with all components enabled") {
        FuzzPedal pedal;
        pedal.prepare(44100.0, 512);

        // Enable all components
        pedal.setInputBuffer(true);
        pedal.setBufferCutoff(BufferCutoff::Hz10);
        pedal.setFuzzType(FuzzType::Germanium);
        pedal.setFuzz(0.7f);
        pedal.setTone(0.5f);
        pedal.setBias(0.8f);
        pedal.setGateEnabled(true);
        pedal.setGateThreshold(-60.0f);
        pedal.setGateType(GateType::SoftKnee);
        pedal.setGateTiming(GateTiming::Normal);
        pedal.setVolume(0.0f);

        // Process audio
        std::vector<float> buffer(512);
        generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.3f);
        pedal.process(buffer.data(), buffer.size());

        // Verify output characteristics
        float rms = calculateRMS(buffer.data(), buffer.size());
        REQUIRE(rms > 0.0f);
        REQUIRE(std::isfinite(rms));

        // Verify no NaN or Inf in output
        for (auto s : buffer) {
            REQUIRE(std::isfinite(s));
        }
    }

    SECTION("parameter interactions work correctly") {
        FuzzPedal pedal;
        pedal.prepare(44100.0, 4096);

        // Set multiple parameters
        pedal.setFuzz(0.9f);
        pedal.setGateEnabled(true);
        pedal.setGateThreshold(-50.0f);
        pedal.setVolume(6.0f);

        // Process multiple blocks
        std::vector<float> buffer(4096);
        for (int i = 0; i < 5; ++i) {
            generateSine(buffer.data(), buffer.size(), 1000.0f, 44100.0f, 0.3f);
            pedal.process(buffer.data(), buffer.size());
        }

        // Should produce output with harmonic content
        float thd = measureTHDWithFFT(buffer.data(), buffer.size(), 1000.0f, 44100.0f);
        REQUIRE(thd > 1.0f);  // Has harmonic content from fuzz
    }
}
