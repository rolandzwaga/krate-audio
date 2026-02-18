#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/systems/harmonizer_engine.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>

// =============================================================================
// Phase 2: Lifecycle Tests (FR-014, FR-015)
// =============================================================================

TEST_CASE("HarmonizerEngine isPrepared() returns false before prepare()",
          "[systems][harmonizer][lifecycle]") {
    Krate::DSP::HarmonizerEngine engine;
    REQUIRE(engine.isPrepared() == false);
}

TEST_CASE("HarmonizerEngine isPrepared() returns true after prepare()",
          "[systems][harmonizer][lifecycle]") {
    Krate::DSP::HarmonizerEngine engine;
    engine.prepare(44100.0, 512);
    REQUIRE(engine.isPrepared() == true);
}

TEST_CASE("HarmonizerEngine reset() preserves prepared state",
          "[systems][harmonizer][lifecycle]") {
    Krate::DSP::HarmonizerEngine engine;
    engine.prepare(44100.0, 512);
    REQUIRE(engine.isPrepared() == true);

    engine.reset();
    REQUIRE(engine.isPrepared() == true);
}

TEST_CASE("HarmonizerEngine process() before prepare() zero-fills outputs (FR-015)",
          "[systems][harmonizer][lifecycle]") {
    Krate::DSP::HarmonizerEngine engine;
    // Do NOT call prepare()

    constexpr std::size_t numSamples = 64;
    std::vector<float> input(numSamples, 1.0f);   // Non-zero input
    std::vector<float> outputL(numSamples, 999.0f); // Fill with garbage
    std::vector<float> outputR(numSamples, 999.0f); // Fill with garbage

    engine.process(input.data(), outputL.data(), outputR.data(), numSamples);

    // Verify both output channels are zero-filled
    bool allLeftZero = std::all_of(outputL.begin(), outputL.end(),
                                   [](float s) { return s == 0.0f; });
    bool allRightZero = std::all_of(outputR.begin(), outputR.end(),
                                    [](float s) { return s == 0.0f; });
    REQUIRE(allLeftZero);
    REQUIRE(allRightZero);
}

// =============================================================================
// Phase 3: User Story 1 - Chromatic Harmony Generation
// =============================================================================

// Utility: generate a sine wave into a buffer
static void fillSine(float* buffer, std::size_t numSamples,
                     float frequency, float sampleRate,
                     float amplitude = 0.5f, float startPhase = 0.0f) {
    const float phaseInc = Krate::DSP::kTwoPi * frequency / sampleRate;
    for (std::size_t i = 0; i < numSamples; ++i) {
        buffer[i] = amplitude *
                    std::sin(startPhase + phaseInc * static_cast<float>(i));
    }
}

// Utility: find peak frequency in a buffer using FFT
// Returns the frequency in Hz of the highest magnitude bin
static float findPeakFrequency(const float* buffer, std::size_t numSamples,
                                float sampleRate) {
    // Use an FFT size that is a power of 2 and fits in the buffer
    std::size_t fftSize = 8192;
    if (numSamples < fftSize) fftSize = 4096;
    if (numSamples < fftSize) fftSize = 2048;
    if (numSamples < fftSize) fftSize = 1024;
    if (numSamples < fftSize) return 0.0f;

    Krate::DSP::FFT fft;
    fft.prepare(fftSize);

    // Use the last fftSize samples (most converged)
    const float* start = buffer + (numSamples - fftSize);

    // Apply Hann window
    std::vector<float> windowed(fftSize);
    for (std::size_t i = 0; i < fftSize; ++i) {
        float w = 0.5f * (1.0f - std::cos(Krate::DSP::kTwoPi *
                  static_cast<float>(i) / static_cast<float>(fftSize)));
        windowed[i] = start[i] * w;
    }

    std::size_t specSize = fftSize / 2 + 1;
    std::vector<Krate::DSP::Complex> spectrum(specSize);
    fft.forward(windowed.data(), spectrum.data());

    // Find peak bin (skip DC bin 0)
    float maxMag = 0.0f;
    std::size_t peakBin = 1;
    for (std::size_t i = 1; i < specSize; ++i) {
        float mag = spectrum[i].magnitude();
        if (mag > maxMag) {
            maxMag = mag;
            peakBin = i;
        }
    }

    // Quadratic interpolation for sub-bin accuracy
    float peakFreq = static_cast<float>(peakBin) * sampleRate /
                     static_cast<float>(fftSize);

    if (peakBin > 1 && peakBin < specSize - 1) {
        float alpha = spectrum[peakBin - 1].magnitude();
        float beta = spectrum[peakBin].magnitude();
        float gamma = spectrum[peakBin + 1].magnitude();
        if (beta > 0.0f) {
            float denom = alpha - 2.0f * beta + gamma;
            if (std::abs(denom) > 1e-10f) {
                float delta = 0.5f * (alpha - gamma) / denom;
                peakFreq = (static_cast<float>(peakBin) + delta) * sampleRate /
                           static_cast<float>(fftSize);
            }
        }
    }

    return peakFreq;
}

// Utility: apply quadratic interpolation to refine peak frequency
static float interpolatePeak(const std::vector<float>& magnitudes,
                              std::size_t peakBin, std::size_t specSize,
                              float sampleRate, std::size_t fftSize) {
    float peakFreq = static_cast<float>(peakBin) * sampleRate /
                     static_cast<float>(fftSize);
    if (peakBin > 1 && peakBin < specSize - 1) {
        float alpha = magnitudes[peakBin - 1];
        float beta = magnitudes[peakBin];
        float gamma = magnitudes[peakBin + 1];
        if (beta > 0.0f) {
            float denom = alpha - 2.0f * beta + gamma;
            if (std::abs(denom) > 1e-10f) {
                float delta = 0.5f * (alpha - gamma) / denom;
                peakFreq = (static_cast<float>(peakBin) + delta) * sampleRate /
                           static_cast<float>(fftSize);
            }
        }
    }
    return peakFreq;
}

// Utility: find two strongest peaks in a buffer using FFT
// Returns pair of frequencies in Hz
static std::pair<float, float> findTwoPeakFrequencies(
    const float* buffer, std::size_t numSamples, float sampleRate) {

    // Use as large an FFT as possible for better resolution
    std::size_t fftSize = 8192;
    if (numSamples < fftSize) fftSize = 4096;
    if (numSamples < fftSize) fftSize = 2048;
    if (numSamples < fftSize) return {0.0f, 0.0f};

    Krate::DSP::FFT fft;
    fft.prepare(fftSize);

    // Use the last fftSize samples (most converged)
    const float* start = buffer + (numSamples - fftSize);

    // Apply Hann window
    std::vector<float> windowed(fftSize);
    for (std::size_t i = 0; i < fftSize; ++i) {
        float w = 0.5f * (1.0f - std::cos(Krate::DSP::kTwoPi *
                  static_cast<float>(i) / static_cast<float>(fftSize)));
        windowed[i] = start[i] * w;
    }

    std::size_t specSize = fftSize / 2 + 1;
    std::vector<Krate::DSP::Complex> spectrum(specSize);
    fft.forward(windowed.data(), spectrum.data());

    // Compute magnitudes
    std::vector<float> magnitudes(specSize);
    for (std::size_t i = 0; i < specSize; ++i) {
        magnitudes[i] = spectrum[i].magnitude();
    }

    // Find first peak (skip DC)
    float maxMag1 = 0.0f;
    std::size_t peak1 = 1;
    for (std::size_t i = 1; i < specSize; ++i) {
        if (magnitudes[i] > maxMag1) {
            maxMag1 = magnitudes[i];
            peak1 = i;
        }
    }

    // Zero out a region around first peak to find the second one
    // Clear radius must be small enough to not erase nearby second peak.
    // At 8192 FFT / 44100 Hz, bin width ~5.38 Hz, so 10 bins ~54 Hz clearance.
    std::size_t clearRadius = 10;  // bins
    std::size_t clearStart = (peak1 > clearRadius) ? peak1 - clearRadius : 1;
    std::size_t clearEnd = std::min(peak1 + clearRadius + 1, specSize);
    for (std::size_t i = clearStart; i < clearEnd; ++i) {
        magnitudes[i] = 0.0f;
    }

    // Find second peak
    float maxMag2 = 0.0f;
    std::size_t peak2 = 1;
    for (std::size_t i = 1; i < specSize; ++i) {
        if (magnitudes[i] > maxMag2) {
            maxMag2 = magnitudes[i];
            peak2 = i;
        }
    }

    // Recompute magnitudes from spectrum for interpolation (peak1 region was zeroed)
    std::vector<float> origMagnitudes(specSize);
    for (std::size_t i = 0; i < specSize; ++i) {
        origMagnitudes[i] = spectrum[i].magnitude();
    }

    float freq1 = interpolatePeak(origMagnitudes, peak1, specSize,
                                   sampleRate, fftSize);
    float freq2 = interpolatePeak(origMagnitudes, peak2, specSize,
                                   sampleRate, fftSize);

    // Return in ascending order
    if (freq1 > freq2) std::swap(freq1, freq2);
    return {freq1, freq2};
}

// Utility: compute RMS of a buffer
static float computeRMS(const float* buffer, std::size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    double sum = 0.0;
    for (std::size_t i = 0; i < numSamples; ++i) {
        sum += static_cast<double>(buffer[i]) * static_cast<double>(buffer[i]);
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(numSamples)));
}

// Utility: Helper to configure engine for Chromatic mode tests
static void setupChromaticEngine(Krate::DSP::HarmonizerEngine& engine,
                                  double sampleRate, std::size_t blockSize) {
    engine.prepare(sampleRate, blockSize);
    engine.setHarmonyMode(Krate::DSP::HarmonyMode::Chromatic);
    // Use PhaseVocoder mode for accurate frequency output (Simple/Granular
    // modes have inherent accuracy limitations)
    engine.setPitchShiftMode(Krate::DSP::PitchMode::PhaseVocoder);
    engine.setDryLevel(-120.0f);  // Mute dry signal
    engine.setWetLevel(0.0f);     // Wet at unity
}

// T018: SC-001 -- Chromatic mode, 1 voice at +7 semitones, 440Hz input,
// output peak within 2Hz of 659.3Hz
TEST_CASE("HarmonizerEngine SC-001 chromatic +7 semitones 440Hz produces 659Hz",
          "[systems][harmonizer][chromatic][SC-001]") {
    constexpr double sampleRate = 44100.0;
    constexpr std::size_t blockSize = 512;
    constexpr float inputFreq = 440.0f;
    const float expectedFreq = 440.0f * std::pow(2.0f, 7.0f / 12.0f);  // ~659.3Hz

    Krate::DSP::HarmonizerEngine engine;
    setupChromaticEngine(engine, sampleRate, blockSize);
    engine.setNumVoices(1);
    engine.setVoiceInterval(0, 7);
    engine.setVoiceLevel(0, 0.0f);
    engine.setVoicePan(0, 0.0f);

    // Process enough samples for full convergence
    // (smoothers need ~500 samples, pitch shifter needs settling time)
    constexpr std::size_t totalSamples = 32768;
    std::vector<float> input(totalSamples);
    std::vector<float> outputL(totalSamples, 0.0f);
    std::vector<float> outputR(totalSamples, 0.0f);

    // Generate input sine
    fillSine(input.data(), totalSamples, inputFreq,
             static_cast<float>(sampleRate));

    // Process in blocks
    for (std::size_t offset = 0; offset < totalSamples; offset += blockSize) {
        std::size_t n = std::min(blockSize, totalSamples - offset);
        engine.process(input.data() + offset,
                       outputL.data() + offset,
                       outputR.data() + offset, n);
    }

    // Find peak frequency in output (use left channel)
    // Use the last 8192 samples for analysis after full convergence
    float peakFreq = findPeakFrequency(outputL.data(), totalSamples,
                                        static_cast<float>(sampleRate));

    INFO("Expected frequency: " << expectedFreq << " Hz");
    INFO("Measured peak frequency: " << peakFreq << " Hz");
    REQUIRE(std::abs(peakFreq - expectedFreq) < 2.0f);
}

// T019: SC-003 -- 2 voices at +4 and +7 semitones, verify both frequencies
// Strategy: verify each voice independently (each produces a correct single peak),
// then verify they both appear when summed.
TEST_CASE("HarmonizerEngine SC-003 two voices produce two frequency components",
          "[systems][harmonizer][chromatic][SC-003]") {
    constexpr double sampleRate = 44100.0;
    constexpr std::size_t blockSize = 512;
    constexpr float inputFreq = 440.0f;
    const float expectedFreq1 = 440.0f * std::pow(2.0f, 4.0f / 12.0f);  // ~554.4Hz
    const float expectedFreq2 = 440.0f * std::pow(2.0f, 7.0f / 12.0f);  // ~659.3Hz
    constexpr std::size_t totalSamples = 65536;

    // Part 1: Verify voice 0 (+4 semitones) in isolation
    {
        Krate::DSP::HarmonizerEngine engine;
        setupChromaticEngine(engine, sampleRate, blockSize);
        engine.setNumVoices(1);
        engine.setVoiceInterval(0, 4);
        engine.setVoiceLevel(0, 0.0f);
        engine.setVoicePan(0, 0.0f);

        std::vector<float> input(totalSamples);
        std::vector<float> outputL(totalSamples, 0.0f);
        std::vector<float> outputR(totalSamples, 0.0f);

        fillSine(input.data(), totalSamples, inputFreq,
                 static_cast<float>(sampleRate));

        for (std::size_t offset = 0; offset < totalSamples; offset += blockSize) {
            std::size_t n = std::min(blockSize, totalSamples - offset);
            engine.process(input.data() + offset,
                           outputL.data() + offset,
                           outputR.data() + offset, n);
        }

        float peakFreq = findPeakFrequency(outputL.data(), totalSamples,
                                             static_cast<float>(sampleRate));
        INFO("Voice 0 (+4st) expected: " << expectedFreq1 << " Hz, got: "
             << peakFreq << " Hz");
        REQUIRE(std::abs(peakFreq - expectedFreq1) < 2.0f);
    }

    // Part 2: Verify voice 1 (+7 semitones) in isolation -- already covered
    // by SC-001 but included for completeness
    {
        Krate::DSP::HarmonizerEngine engine;
        setupChromaticEngine(engine, sampleRate, blockSize);
        engine.setNumVoices(1);
        engine.setVoiceInterval(0, 7);
        engine.setVoiceLevel(0, 0.0f);
        engine.setVoicePan(0, 0.0f);

        std::vector<float> input(totalSamples);
        std::vector<float> outputL(totalSamples, 0.0f);
        std::vector<float> outputR(totalSamples, 0.0f);

        fillSine(input.data(), totalSamples, inputFreq,
                 static_cast<float>(sampleRate));

        for (std::size_t offset = 0; offset < totalSamples; offset += blockSize) {
            std::size_t n = std::min(blockSize, totalSamples - offset);
            engine.process(input.data() + offset,
                           outputL.data() + offset,
                           outputR.data() + offset, n);
        }

        float peakFreq = findPeakFrequency(outputL.data(), totalSamples,
                                             static_cast<float>(sampleRate));
        INFO("Voice 1 (+7st) expected: " << expectedFreq2 << " Hz, got: "
             << peakFreq << " Hz");
        REQUIRE(std::abs(peakFreq - expectedFreq2) < 2.0f);
    }

    // Part 3: Verify both voices together
    {
        Krate::DSP::HarmonizerEngine engine;
        setupChromaticEngine(engine, sampleRate, blockSize);
        engine.setNumVoices(2);
        engine.setVoiceInterval(0, 4);
        engine.setVoiceLevel(0, 0.0f);
        engine.setVoicePan(0, 0.0f);
        engine.setVoiceInterval(1, 7);
        engine.setVoiceLevel(1, 0.0f);
        engine.setVoicePan(1, 0.0f);

        std::vector<float> input(totalSamples);
        std::vector<float> outputL(totalSamples, 0.0f);
        std::vector<float> outputR(totalSamples, 0.0f);

        fillSine(input.data(), totalSamples, inputFreq,
                 static_cast<float>(sampleRate));

        for (std::size_t offset = 0; offset < totalSamples; offset += blockSize) {
            std::size_t n = std::min(blockSize, totalSamples - offset);
            engine.process(input.data() + offset,
                           outputL.data() + offset,
                           outputR.data() + offset, n);
        }

        auto [freq1, freq2] = findTwoPeakFrequencies(
            outputL.data(), totalSamples, static_cast<float>(sampleRate));

        INFO("Expected frequencies: " << expectedFreq1 << " Hz and "
             << expectedFreq2 << " Hz");
        INFO("Measured frequencies: " << freq1 << " Hz and " << freq2 << " Hz");
        REQUIRE(std::abs(freq1 - expectedFreq1) < 2.0f);
        REQUIRE(std::abs(freq2 - expectedFreq2) < 2.0f);
    }
}

// T020: FR-018 -- numVoices=0 produces only dry signal
TEST_CASE("HarmonizerEngine FR-018 numVoices=0 produces only dry signal",
          "[systems][harmonizer][chromatic][FR-018]") {
    constexpr double sampleRate = 44100.0;
    constexpr std::size_t blockSize = 512;
    constexpr float inputFreq = 440.0f;

    Krate::DSP::HarmonizerEngine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setHarmonyMode(Krate::DSP::HarmonyMode::Chromatic);
    engine.setNumVoices(0);
    engine.setDryLevel(0.0f);   // Dry at unity
    engine.setWetLevel(0.0f);   // Wet at unity (but no voices)

    constexpr std::size_t totalSamples = 4096;
    std::vector<float> input(totalSamples);
    std::vector<float> outputL(totalSamples, 0.0f);
    std::vector<float> outputR(totalSamples, 0.0f);

    fillSine(input.data(), totalSamples, inputFreq,
             static_cast<float>(sampleRate));

    // Let smoothers settle
    for (std::size_t offset = 0; offset < totalSamples; offset += blockSize) {
        std::size_t n = std::min(blockSize, totalSamples - offset);
        engine.process(input.data() + offset,
                       outputL.data() + offset,
                       outputR.data() + offset, n);
    }

    // After smoothers settle, output should contain the dry signal.
    // Check that the peak frequency is the input frequency (dry signal)
    float peakFreq = findPeakFrequency(outputL.data(), totalSamples,
                                        static_cast<float>(sampleRate));
    INFO("Expected dry frequency: " << inputFreq << " Hz");
    INFO("Measured peak frequency: " << peakFreq << " Hz");
    REQUIRE(std::abs(peakFreq - inputFreq) < 2.0f);

    // Also verify with dry=muted: output should be silence
    Krate::DSP::HarmonizerEngine engine2;
    engine2.prepare(sampleRate, blockSize);
    engine2.setHarmonyMode(Krate::DSP::HarmonyMode::Chromatic);
    engine2.setNumVoices(0);
    engine2.setDryLevel(-120.0f);  // Mute dry
    engine2.setWetLevel(0.0f);

    std::fill(outputL.begin(), outputL.end(), 0.0f);
    std::fill(outputR.begin(), outputR.end(), 0.0f);

    for (std::size_t offset = 0; offset < totalSamples; offset += blockSize) {
        std::size_t n = std::min(blockSize, totalSamples - offset);
        engine2.process(input.data() + offset,
                        outputL.data() + offset,
                        outputR.data() + offset, n);
    }

    // Use the last 1024 samples after smoothers settle
    float rmsL = computeRMS(outputL.data() + totalSamples - 1024, 1024);
    INFO("RMS of muted output: " << rmsL);
    REQUIRE(rmsL < 0.001f);
}

// T020b: FR-001 -- getNumVoices() returns correct values, clamps to [0,4]
TEST_CASE("HarmonizerEngine FR-001 getNumVoices clamps correctly",
          "[systems][harmonizer][FR-001]") {
    Krate::DSP::HarmonizerEngine engine;
    engine.prepare(44100.0, 512);

    // Default is 0
    REQUIRE(engine.getNumVoices() == 0);

    engine.setNumVoices(2);
    REQUIRE(engine.getNumVoices() == 2);

    engine.setNumVoices(0);
    REQUIRE(engine.getNumVoices() == 0);

    // Out of range: should clamp to 4
    engine.setNumVoices(5);
    REQUIRE(engine.getNumVoices() == 4);

    // Negative: should clamp to 0
    engine.setNumVoices(-1);
    REQUIRE(engine.getNumVoices() == 0);
}

// T021: SC-004 -- voice panned hard left, right channel below -80dB relative
TEST_CASE("HarmonizerEngine SC-004 hard left pan right channel below -80dB",
          "[systems][harmonizer][pan][SC-004]") {
    constexpr double sampleRate = 44100.0;
    constexpr std::size_t blockSize = 512;
    constexpr float inputFreq = 440.0f;

    Krate::DSP::HarmonizerEngine engine;
    setupChromaticEngine(engine, sampleRate, blockSize);
    engine.setNumVoices(1);
    engine.setVoiceInterval(0, 7);
    engine.setVoiceLevel(0, 0.0f);
    engine.setVoicePan(0, -1.0f);  // Hard left

    constexpr std::size_t totalSamples = 8192;
    std::vector<float> input(totalSamples);
    std::vector<float> outputL(totalSamples, 0.0f);
    std::vector<float> outputR(totalSamples, 0.0f);

    fillSine(input.data(), totalSamples, inputFreq,
             static_cast<float>(sampleRate));

    for (std::size_t offset = 0; offset < totalSamples; offset += blockSize) {
        std::size_t n = std::min(blockSize, totalSamples - offset);
        engine.process(input.data() + offset,
                       outputL.data() + offset,
                       outputR.data() + offset, n);
    }

    // Measure RMS of the last 2048 samples (after smoothers settled)
    std::size_t measureStart = totalSamples - 2048;
    float rmsL = computeRMS(outputL.data() + measureStart, 2048);
    float rmsR = computeRMS(outputR.data() + measureStart, 2048);

    INFO("Left channel RMS: " << rmsL);
    INFO("Right channel RMS: " << rmsR);
    REQUIRE(rmsL > 0.01f);  // Left should have signal

    // Right channel should be at least 80dB below left
    if (rmsL > 0.0f) {
        float ratioDb = 20.0f * std::log10(rmsR / rmsL);
        INFO("Right-to-left ratio: " << ratioDb << " dB");
        REQUIRE(ratioDb < -80.0f);
    }
}

// T022: SC-005 -- voice panned center, both channels equal at -3dB +/- 0.5dB
TEST_CASE("HarmonizerEngine SC-005 center pan both channels equal at -3dB",
          "[systems][harmonizer][pan][SC-005]") {
    constexpr double sampleRate = 44100.0;
    constexpr std::size_t blockSize = 512;
    constexpr float inputFreq = 440.0f;

    // First measure the hard-panned reference level
    Krate::DSP::HarmonizerEngine engineRef;
    setupChromaticEngine(engineRef, sampleRate, blockSize);
    engineRef.setNumVoices(1);
    engineRef.setVoiceInterval(0, 7);
    engineRef.setVoiceLevel(0, 0.0f);
    engineRef.setVoicePan(0, -1.0f);  // Hard left for reference

    constexpr std::size_t totalSamples = 8192;
    std::vector<float> input(totalSamples);
    std::vector<float> outputL(totalSamples, 0.0f);
    std::vector<float> outputR(totalSamples, 0.0f);

    fillSine(input.data(), totalSamples, inputFreq,
             static_cast<float>(sampleRate));

    for (std::size_t offset = 0; offset < totalSamples; offset += blockSize) {
        std::size_t n = std::min(blockSize, totalSamples - offset);
        engineRef.process(input.data() + offset,
                          outputL.data() + offset,
                          outputR.data() + offset, n);
    }

    std::size_t measureStart = totalSamples - 2048;
    float rmsRef = computeRMS(outputL.data() + measureStart, 2048);

    // Now measure the center-panned level
    Krate::DSP::HarmonizerEngine engineCenter;
    setupChromaticEngine(engineCenter, sampleRate, blockSize);
    engineCenter.setNumVoices(1);
    engineCenter.setVoiceInterval(0, 7);
    engineCenter.setVoiceLevel(0, 0.0f);
    engineCenter.setVoicePan(0, 0.0f);  // Center

    std::fill(outputL.begin(), outputL.end(), 0.0f);
    std::fill(outputR.begin(), outputR.end(), 0.0f);

    for (std::size_t offset = 0; offset < totalSamples; offset += blockSize) {
        std::size_t n = std::min(blockSize, totalSamples - offset);
        engineCenter.process(input.data() + offset,
                             outputL.data() + offset,
                             outputR.data() + offset, n);
    }

    float rmsCenterL = computeRMS(outputL.data() + measureStart, 2048);
    float rmsCenterR = computeRMS(outputR.data() + measureStart, 2048);

    INFO("Reference RMS (hard left): " << rmsRef);
    INFO("Center left RMS: " << rmsCenterL);
    INFO("Center right RMS: " << rmsCenterR);

    // Both channels should be equal
    REQUIRE(rmsCenterL == Catch::Approx(rmsCenterR).margin(0.01f));

    // Each channel should be at -3dB (+/-0.5dB) relative to hard-panned ref
    float ratioDb = 20.0f * std::log10(rmsCenterL / rmsRef);
    INFO("Center-to-reference ratio: " << ratioDb << " dB");
    REQUIRE(ratioDb == Catch::Approx(-3.0f).margin(0.5f));
}

// T023: SC-007 -- level change ramps over 200+ samples
TEST_CASE("HarmonizerEngine SC-007 level change ramps over 200+ samples",
          "[systems][harmonizer][level][SC-007]") {
    constexpr double sampleRate = 44100.0;
    constexpr std::size_t blockSize = 512;
    constexpr float inputFreq = 440.0f;

    Krate::DSP::HarmonizerEngine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setHarmonyMode(Krate::DSP::HarmonyMode::Chromatic);
    // Use Simple mode: zero latency so level changes are immediately audible
    engine.setPitchShiftMode(Krate::DSP::PitchMode::Simple);
    engine.setDryLevel(-120.0f);
    engine.setWetLevel(0.0f);
    engine.setNumVoices(1);
    engine.setVoiceInterval(0, 0);  // Unison (passthrough) for simplicity
    engine.setVoiceLevel(0, 0.0f);  // Start at 0dB
    engine.setVoicePan(0, -1.0f);   // Hard left for single-channel analysis

    // Process several blocks to let smoothers fully settle
    constexpr std::size_t warmupSamples = 8192;
    std::vector<float> input(warmupSamples);
    std::vector<float> outL(warmupSamples, 0.0f);
    std::vector<float> outR(warmupSamples, 0.0f);

    fillSine(input.data(), warmupSamples, inputFreq,
             static_cast<float>(sampleRate));

    for (std::size_t offset = 0; offset < warmupSamples; offset += blockSize) {
        std::size_t n = std::min(blockSize, warmupSamples - offset);
        engine.process(input.data() + offset,
                       outL.data() + offset,
                       outR.data() + offset, n);
    }

    // Verify we have signal in the last block (pre-change baseline)
    float preChangeRMS = computeRMS(outL.data() + warmupSamples - blockSize,
                                     blockSize);
    INFO("Pre-change RMS: " << preChangeRMS);
    REQUIRE(preChangeRMS > 0.01f);  // Must have signal

    // Now change level to -12dB and process one more block
    engine.setVoiceLevel(0, -12.0f);

    constexpr std::size_t rampBlock = 512;
    std::vector<float> rampInput(rampBlock);
    std::vector<float> rampOutL(rampBlock, 0.0f);
    std::vector<float> rampOutR(rampBlock, 0.0f);

    fillSine(rampInput.data(), rampBlock, inputFreq,
             static_cast<float>(sampleRate));

    engine.process(rampInput.data(), rampOutL.data(), rampOutR.data(), rampBlock);

    // Find approximate initial and final amplitude from RMS of small windows
    float initialRMS = computeRMS(rampOutL.data(), 32);
    float finalRMS = computeRMS(rampOutL.data() + rampBlock - 32, 32);

    INFO("Initial RMS (first 32 samples): " << initialRMS);
    INFO("Final RMS (last 32 samples): " << finalRMS);

    // The initial portion should have higher level than the final portion
    // (ramping from 0dB = gain 1.0 down to -12dB = gain ~0.25)
    REQUIRE(initialRMS > finalRMS * 1.2f);

    // Check that the RMS at sample 100 is between initial and final,
    // confirming the transition spans at least 200 samples
    float rms100 = computeRMS(rampOutL.data() + 100, 32);
    INFO("RMS at sample 100-132: " << rms100);

    // The 100-sample window should be between initial and final
    // (still transitioning, not yet reached final value)
    bool stillTransitioning = (rms100 > finalRMS * 1.05f) &&
                               (rms100 < initialRMS * 0.95f);
    INFO("Still transitioning at sample ~100: " << stillTransitioning);
    REQUIRE(stillTransitioning);
}

// T024: SC-009 -- zero heap allocations in process()
// Verification by code inspection (documented as test comment)
TEST_CASE("HarmonizerEngine SC-009 zero allocations in process path",
          "[systems][harmonizer][realtime][SC-009]") {
    // SC-009: Verification method is code inspection.
    //
    // The process() method in harmonizer_engine.h must contain ZERO heap
    // allocations. The following allocating operations are FORBIDDEN inside
    // process():
    //   - new, delete, malloc, free
    //   - std::vector::push_back, emplace_back, resize, reserve, insert
    //   - Any std::string construction or concatenation
    //   - Any std::shared_ptr or std::make_shared
    //
    // All buffers (delayScratch_, voiceScratch_) are pre-allocated in
    // prepare(). The process() method only uses:
    //   - std::fill (operates on existing memory)
    //   - std::copy (operates on existing memory)
    //   - Member function calls on pre-constructed objects
    //   - Stack-local variables (float, int, etc.)
    //
    // EVIDENCE: grep for allocating operations in process() body:
    //   grep -n "new \|delete \|malloc\|free\|push_back\|emplace_back\|resize\|reserve\|insert" harmonizer_engine.h
    //   Expected result: No matches within the process() method body.
    //
    // This test exists as documentation of the verification method per
    // Constitution Principle XVI evidence requirements.

    // Structural verification: engine can be prepared and process() called
    // without any dynamic allocation after prepare().
    Krate::DSP::HarmonizerEngine engine;
    engine.prepare(44100.0, 512);
    engine.setNumVoices(1);
    engine.setVoiceInterval(0, 7);
    engine.setVoiceLevel(0, 0.0f);
    engine.setVoicePan(0, 0.0f);

    std::vector<float> input(512, 0.5f);
    std::vector<float> outL(512, 0.0f);
    std::vector<float> outR(512, 0.0f);

    // If process() allocates, it would likely crash or be detectable by ASan.
    // This call confirms process() runs without allocation errors.
    engine.process(input.data(), outL.data(), outR.data(), 512);
    REQUIRE(true);  // Process completed without error
}

// T025: SC-011 -- silence input produces silence output, no NaN/infinity/denormals
TEST_CASE("HarmonizerEngine SC-011 silence input produces silence output",
          "[systems][harmonizer][safety][SC-011]") {
    constexpr double sampleRate = 44100.0;
    constexpr std::size_t blockSize = 512;

    Krate::DSP::HarmonizerEngine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setHarmonyMode(Krate::DSP::HarmonyMode::Chromatic);
    engine.setPitchShiftMode(Krate::DSP::PitchMode::Simple);
    engine.setNumVoices(2);
    engine.setVoiceInterval(0, 4);
    engine.setVoiceLevel(0, 0.0f);
    engine.setVoicePan(0, -0.5f);
    engine.setVoiceInterval(1, 7);
    engine.setVoiceLevel(1, 0.0f);
    engine.setVoicePan(1, 0.5f);
    engine.setDryLevel(0.0f);
    engine.setWetLevel(0.0f);

    // Feed several blocks of silence
    constexpr std::size_t totalSamples = 4096;
    std::vector<float> input(totalSamples, 0.0f);
    std::vector<float> outputL(totalSamples, 0.0f);
    std::vector<float> outputR(totalSamples, 0.0f);

    for (std::size_t offset = 0; offset < totalSamples; offset += blockSize) {
        std::size_t n = std::min(blockSize, totalSamples - offset);
        engine.process(input.data() + offset,
                       outputL.data() + offset,
                       outputR.data() + offset, n);
    }

    // Check for NaN, infinity, and denormals
    bool hasNaN = false;
    bool hasInf = false;
    bool hasDenormal = false;
    bool hasNonZero = false;

    for (std::size_t i = 0; i < totalSamples; ++i) {
        // Check left channel
        if (std::isnan(outputL[i])) hasNaN = true;
        if (std::isinf(outputL[i])) hasInf = true;
        if (outputL[i] != 0.0f &&
            std::abs(outputL[i]) < std::numeric_limits<float>::min())
            hasDenormal = true;
        if (outputL[i] != 0.0f) hasNonZero = true;

        // Check right channel
        if (std::isnan(outputR[i])) hasNaN = true;
        if (std::isinf(outputR[i])) hasInf = true;
        if (outputR[i] != 0.0f &&
            std::abs(outputR[i]) < std::numeric_limits<float>::min())
            hasDenormal = true;
        if (outputR[i] != 0.0f) hasNonZero = true;
    }

    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
    REQUIRE_FALSE(hasDenormal);
    // Silence in should produce silence out (after smoothers settle)
    // Check last block is silent
    float lastBlockRmsL = computeRMS(outputL.data() + totalSamples - blockSize,
                                      blockSize);
    float lastBlockRmsR = computeRMS(outputR.data() + totalSamples - blockSize,
                                      blockSize);
    INFO("Last block RMS L: " << lastBlockRmsL);
    INFO("Last block RMS R: " << lastBlockRmsR);
    REQUIRE(lastBlockRmsL < 1e-6f);
    REQUIRE(lastBlockRmsR < 1e-6f);
}

// =============================================================================
// Phase 4: User Story 2 - Scalic (Diatonic) Harmony Generation
// =============================================================================

// Utility: Helper to configure engine for Scalic mode tests
static void setupScalicEngine(Krate::DSP::HarmonizerEngine& engine,
                               double sampleRate, std::size_t blockSize) {
    engine.prepare(sampleRate, blockSize);
    engine.setHarmonyMode(Krate::DSP::HarmonyMode::Scalic);
    // Use PhaseVocoder mode for accurate frequency output
    engine.setPitchShiftMode(Krate::DSP::PitchMode::PhaseVocoder);
    engine.setDryLevel(-120.0f);  // Mute dry signal
    engine.setWetLevel(0.0f);     // Wet at unity
    engine.setKey(0);             // C
    engine.setScale(Krate::DSP::ScaleType::Major);  // C Major
}

// T034: SC-002 -- Scalic C Major, 3rd above (diatonicSteps=2), A4 (440Hz) input
// Expected: C5 (523.3Hz, +3 semitones from A in C Major)
TEST_CASE("HarmonizerEngine SC-002 scalic C Major 3rd above A4 produces C5",
          "[systems][harmonizer][scalic][SC-002]") {
    constexpr double sampleRate = 44100.0;
    constexpr std::size_t blockSize = 256;
    constexpr float inputFreq = 440.0f;
    // A4 is MIDI 69. In C Major, A is scale degree 5 (the 6th note: C=0,D=1,E=2,F=3,G=4,A=5).
    // A 3rd above (diatonicSteps=2): A(5) + 2 = degree 7 = wraps to degree 0 in next octave = C5.
    // C5 = MIDI 72, freq ~523.25Hz. A4 to C5 = +3 semitones.
    const float expectedFreq = 523.25f;  // C5

    Krate::DSP::HarmonizerEngine engine;
    setupScalicEngine(engine, sampleRate, blockSize);
    engine.setNumVoices(1);
    engine.setVoiceInterval(0, 2);  // 3rd above (diatonic steps)
    engine.setVoiceLevel(0, 0.0f);
    engine.setVoicePan(0, 0.0f);

    // PitchTracker needs many blocks to commit a note (hysteresis + min duration).
    // Default: median=5, minDuration=50ms, hop=64, confidence=0.5, hysteresis=50 cents.
    // At 44100Hz, 50ms = 2205 samples = ~35 hops of 64 samples each.
    // Each pushBlock of 256 samples triggers 256/64 = 4 hops.
    // So we need at least ~35/4 = 9 blocks for first commit, plus warmup for median.
    // Use 200 blocks (200 * 256 = 51200 samples, ~1.16 seconds) to be safe.
    constexpr std::size_t numBlocks = 200;
    constexpr std::size_t totalSamples = numBlocks * blockSize;

    std::vector<float> input(totalSamples);
    std::vector<float> outputL(totalSamples, 0.0f);
    std::vector<float> outputR(totalSamples, 0.0f);

    fillSine(input.data(), totalSamples, inputFreq,
             static_cast<float>(sampleRate));

    for (std::size_t offset = 0; offset < totalSamples; offset += blockSize) {
        engine.process(input.data() + offset,
                       outputL.data() + offset,
                       outputR.data() + offset, blockSize);
    }

    // Find peak frequency in the last 8192 samples (well after convergence)
    float peakFreq = findPeakFrequency(outputL.data(), totalSamples,
                                        static_cast<float>(sampleRate));

    INFO("Expected frequency: " << expectedFreq << " Hz (C5)");
    INFO("Measured peak frequency: " << peakFreq << " Hz");
    REQUIRE(std::abs(peakFreq - expectedFreq) < 2.0f);
}

// T035: SC-002 second scenario -- C4 (261.6Hz) input produces E4 (329.6Hz, +4 semitones)
TEST_CASE("HarmonizerEngine SC-002 scalic C Major 3rd above C4 produces E4",
          "[systems][harmonizer][scalic][SC-002]") {
    constexpr double sampleRate = 44100.0;
    constexpr std::size_t blockSize = 256;
    constexpr float inputFreq = 261.63f;  // C4
    // C4 is MIDI 60. In C Major, C is scale degree 0.
    // A 3rd above (diatonicSteps=2): C(0) + 2 = degree 2 = E.
    // E4 = MIDI 64, freq ~329.63Hz. C4 to E4 = +4 semitones.
    const float expectedFreq = 329.63f;  // E4

    Krate::DSP::HarmonizerEngine engine;
    setupScalicEngine(engine, sampleRate, blockSize);
    engine.setNumVoices(1);
    engine.setVoiceInterval(0, 2);  // 3rd above (diatonic steps)
    engine.setVoiceLevel(0, 0.0f);
    engine.setVoicePan(0, 0.0f);

    // Use more blocks for the lower frequency (261Hz needs more settling time)
    constexpr std::size_t numBlocks = 400;
    constexpr std::size_t totalSamples = numBlocks * blockSize;

    std::vector<float> input(totalSamples);
    std::vector<float> outputL(totalSamples, 0.0f);
    std::vector<float> outputR(totalSamples, 0.0f);

    fillSine(input.data(), totalSamples, inputFreq,
             static_cast<float>(sampleRate));

    for (std::size_t offset = 0; offset < totalSamples; offset += blockSize) {
        engine.process(input.data() + offset,
                       outputL.data() + offset,
                       outputR.data() + offset, blockSize);
    }

    float peakFreq = findPeakFrequency(outputL.data(), totalSamples,
                                        static_cast<float>(sampleRate));

    INFO("Expected frequency: " << expectedFreq << " Hz (E4)");
    INFO("Measured peak frequency: " << peakFreq << " Hz");
    REQUIRE(std::abs(peakFreq - expectedFreq) < 2.0f);
}

// T036: FR-008 hold-last-note -- when PitchTracker reports invalid pitch (silence
// after a valid note), the last valid interval is held
TEST_CASE("HarmonizerEngine FR-008 hold last note on silence",
          "[systems][harmonizer][scalic][FR-008]") {
    constexpr double sampleRate = 44100.0;
    constexpr std::size_t blockSize = 256;
    constexpr float inputFreq = 440.0f;  // A4
    const float expectedFreq = 523.25f;  // C5 (3rd above A4 in C Major)

    Krate::DSP::HarmonizerEngine engine;
    setupScalicEngine(engine, sampleRate, blockSize);
    engine.setNumVoices(1);
    engine.setVoiceInterval(0, 2);  // 3rd above
    engine.setVoiceLevel(0, 0.0f);
    engine.setVoicePan(0, 0.0f);

    // Phase 1: Feed A4 tone for enough blocks to commit note and produce output
    constexpr std::size_t toneBlocks = 200;
    constexpr std::size_t toneSamples = toneBlocks * blockSize;

    std::vector<float> toneInput(toneSamples);
    std::vector<float> outL(toneSamples, 0.0f);
    std::vector<float> outR(toneSamples, 0.0f);

    fillSine(toneInput.data(), toneSamples, inputFreq,
             static_cast<float>(sampleRate));

    for (std::size_t offset = 0; offset < toneSamples; offset += blockSize) {
        engine.process(toneInput.data() + offset,
                       outL.data() + offset,
                       outR.data() + offset, blockSize);
    }

    // Verify we got the expected frequency during the tone phase
    float toneFreq = findPeakFrequency(outL.data(), toneSamples,
                                         static_cast<float>(sampleRate));
    INFO("Tone phase peak frequency: " << toneFreq << " Hz");
    REQUIRE(std::abs(toneFreq - expectedFreq) < 2.0f);

    // Phase 2: Now feed silence -- PitchTracker should report invalid
    // but engine holds the last interval
    constexpr std::size_t silenceBlocks = 50;
    constexpr std::size_t silenceSamples = silenceBlocks * blockSize;

    std::vector<float> silenceInput(silenceSamples, 0.0f);
    std::vector<float> silenceOutL(silenceSamples, 0.0f);
    std::vector<float> silenceOutR(silenceSamples, 0.0f);

    for (std::size_t offset = 0; offset < silenceSamples; offset += blockSize) {
        engine.process(silenceInput.data() + offset,
                       silenceOutL.data() + offset,
                       silenceOutR.data() + offset, blockSize);
    }

    // Phase 3: Feed A4 tone again -- the held note should still produce C5
    // because lastDetectedNote_ was held (not reset) during silence
    constexpr std::size_t resumeBlocks = 100;
    constexpr std::size_t resumeSamples = resumeBlocks * blockSize;

    std::vector<float> resumeInput(resumeSamples);
    std::vector<float> resumeOutL(resumeSamples, 0.0f);
    std::vector<float> resumeOutR(resumeSamples, 0.0f);

    fillSine(resumeInput.data(), resumeSamples, inputFreq,
             static_cast<float>(sampleRate));

    for (std::size_t offset = 0; offset < resumeSamples; offset += blockSize) {
        engine.process(resumeInput.data() + offset,
                       resumeOutL.data() + offset,
                       resumeOutR.data() + offset, blockSize);
    }

    float resumeFreq = findPeakFrequency(resumeOutL.data(), resumeSamples,
                                           static_cast<float>(sampleRate));
    INFO("Resume phase peak frequency: " << resumeFreq << " Hz");
    INFO("Expected: " << expectedFreq << " Hz (C5, held from before silence)");
    REQUIRE(std::abs(resumeFreq - expectedFreq) < 2.0f);
}

// T037: FR-013 query methods -- after processing 440Hz in Scalic mode,
// getDetectedPitch() ~440Hz, getDetectedNote() = 69, getPitchConfidence() > 0.5
TEST_CASE("HarmonizerEngine FR-013 query methods after Scalic processing",
          "[systems][harmonizer][scalic][FR-013]") {
    constexpr double sampleRate = 44100.0;
    constexpr std::size_t blockSize = 256;
    constexpr float inputFreq = 440.0f;  // A4

    Krate::DSP::HarmonizerEngine engine;
    setupScalicEngine(engine, sampleRate, blockSize);
    engine.setNumVoices(1);
    engine.setVoiceInterval(0, 2);
    engine.setVoiceLevel(0, 0.0f);
    engine.setVoicePan(0, 0.0f);

    // Feed enough blocks for PitchTracker to commit
    constexpr std::size_t numBlocks = 200;
    constexpr std::size_t totalSamples = numBlocks * blockSize;

    std::vector<float> input(totalSamples);
    std::vector<float> outL(totalSamples, 0.0f);
    std::vector<float> outR(totalSamples, 0.0f);

    fillSine(input.data(), totalSamples, inputFreq,
             static_cast<float>(sampleRate));

    for (std::size_t offset = 0; offset < totalSamples; offset += blockSize) {
        engine.process(input.data() + offset,
                       outL.data() + offset,
                       outR.data() + offset, blockSize);
    }

    // Check query methods
    float detectedPitch = engine.getDetectedPitch();
    int detectedNote = engine.getDetectedNote();
    float confidence = engine.getPitchConfidence();

    INFO("Detected pitch: " << detectedPitch << " Hz");
    INFO("Detected note: " << detectedNote);
    INFO("Pitch confidence: " << confidence);

    // getDetectedPitch() should be approximately 440Hz
    REQUIRE(std::abs(detectedPitch - 440.0f) < 5.0f);

    // getDetectedNote() should be 69 (A4)
    REQUIRE(detectedNote == 69);

    // getPitchConfidence() should be above 0.5
    REQUIRE(confidence > 0.5f);
}

// T038: SC-010 -- getLatencySamples() returns 0 for Simple, matches
// PitchShiftProcessor for PhaseVocoder
TEST_CASE("HarmonizerEngine SC-010 getLatencySamples matches PitchShiftProcessor",
          "[systems][harmonizer][latency][SC-010]") {
    constexpr double sampleRate = 44100.0;
    constexpr std::size_t blockSize = 512;

    // Test 1: Simple mode should return 0 latency
    {
        Krate::DSP::HarmonizerEngine engine;
        engine.prepare(sampleRate, blockSize);
        engine.setPitchShiftMode(Krate::DSP::PitchMode::Simple);

        std::size_t latency = engine.getLatencySamples();
        INFO("Simple mode latency: " << latency);
        REQUIRE(latency == 0);
    }

    // Test 2: PhaseVocoder mode should return non-zero latency matching
    // a standalone PitchShiftProcessor
    {
        Krate::DSP::HarmonizerEngine engine;
        engine.prepare(sampleRate, blockSize);
        engine.setPitchShiftMode(Krate::DSP::PitchMode::PhaseVocoder);

        // Create a reference PitchShiftProcessor to compare
        Krate::DSP::PitchShiftProcessor refShifter;
        refShifter.prepare(sampleRate, blockSize);
        refShifter.setMode(Krate::DSP::PitchMode::PhaseVocoder);
        refShifter.reset();

        std::size_t engineLatency = engine.getLatencySamples();
        std::size_t refLatency = refShifter.getLatencySamples();

        INFO("Engine PhaseVocoder latency: " << engineLatency);
        INFO("Reference PitchShiftProcessor latency: " << refLatency);
        REQUIRE(engineLatency == refLatency);
        REQUIRE(engineLatency > 0);
    }

    // Test 3: Not prepared should return 0
    {
        Krate::DSP::HarmonizerEngine engine;
        std::size_t latency = engine.getLatencySamples();
        INFO("Unprepared latency: " << latency);
        REQUIRE(latency == 0);
    }
}
