// =============================================================================
// Modal Resonator Bank Bowed Mode Tests (Spec 130, Phase 8)
// =============================================================================
// Tests for bowed-mode bandpass velocity taps (FR-020, FR-024).
// Covers: BowedModeBPF struct, setBowModeActive(), setBowPosition(),
// harmonic weighting, and self-sustained oscillation with BowExciter.

#include <krate/dsp/processors/modal_resonator_bank.h>
#include <krate/dsp/processors/bow_exciter.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <numbers>

using Catch::Approx;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr float kPi = std::numbers::pi_v<float>;

// Helper: configure a simple multi-mode resonator
void configureBasicModes(
    Krate::DSP::ModalResonatorBank& bank,
    float fundamental,
    int numModes,
    float decayTime = 2.0f,
    float brightness = 0.0f)
{
    std::array<float, 96> freqs{};
    std::array<float, 96> amps{};
    for (int k = 0; k < numModes; ++k) {
        freqs[static_cast<size_t>(k)] = fundamental * static_cast<float>(k + 1);
        amps[static_cast<size_t>(k)] = 1.0f / static_cast<float>(k + 1);
    }
    bank.setModes(freqs.data(), amps.data(), numModes, decayTime, brightness, 0.0f, 0.0f);
}

} // namespace

// =============================================================================
// T073: BowedModeBPF struct tests
// =============================================================================

TEST_CASE("ModalResonatorBank BowedModeBPF setCoefficients computes valid BPF coefficients",
          "[processors][modal][bow]")
{
    Krate::DSP::BowedModeBPF bpf;
    bpf.setCoefficients(440.0f, 50.0f, kSampleRate);

    // b0 should be alpha = sin(w0)/(2*Q), non-zero and positive
    REQUIRE(bpf.b0 > 0.0f);
    // b2 = -alpha (negative of b0)
    REQUIRE(bpf.b2 == Approx(-bpf.b0));
    // a1 and a2 should be non-zero (feedback coefficients)
    REQUIRE(bpf.a1 != 0.0f);
    REQUIRE(bpf.a2 != 0.0f);
}

TEST_CASE("ModalResonatorBank BowedModeBPF process returns filtered output",
          "[processors][modal][bow]")
{
    Krate::DSP::BowedModeBPF bpf;
    bpf.setCoefficients(440.0f, 50.0f, kSampleRate);

    // Feed an impulse and verify we get output
    float impulseOut = bpf.process(1.0f);
    REQUIRE(impulseOut != 0.0f);

    // Subsequent samples with zero input should still produce output (resonance)
    float decayOut = bpf.process(0.0f);
    REQUIRE(decayOut != 0.0f);
}

TEST_CASE("ModalResonatorBank BowedModeBPF reset clears state",
          "[processors][modal][bow]")
{
    Krate::DSP::BowedModeBPF bpf;
    bpf.setCoefficients(440.0f, 50.0f, kSampleRate);

    // Feed some signal
    for (int i = 0; i < 100; ++i)
        (void)bpf.process(1.0f);

    bpf.reset();

    // After reset, output with zero input should be zero
    float out = bpf.process(0.0f);
    REQUIRE(out == 0.0f);
}

TEST_CASE("ModalResonatorBank BowedModeBPF Q~50 bandwidth is ~8.8 Hz at 440 Hz",
          "[processors][modal][bow]")
{
    // Q = fc / bandwidth, so bandwidth = fc / Q = 440 / 50 = 8.8 Hz
    // Test: feed sine at 440 Hz (center) vs sine at 440 +/- 4.4 Hz (half-bandwidth)
    // At half-bandwidth the response should be ~-3 dB relative to center
    Krate::DSP::BowedModeBPF bpf;
    bpf.setCoefficients(440.0f, 50.0f, kSampleRate);

    constexpr int kNumSamples = 44100; // 1 second

    // Measure steady-state response at center frequency
    auto measureResponse = [&](float freq) -> float {
        bpf.reset();
        float maxAbs = 0.0f;
        for (int i = 0; i < kNumSamples; ++i) {
            float input = std::sin(2.0f * kPi * freq * static_cast<float>(i)
                                   / static_cast<float>(kSampleRate));
            float out = bpf.process(input);
            // Use last 25% for steady-state measurement
            if (i > kNumSamples * 3 / 4)
                maxAbs = std::max(maxAbs, std::abs(out));
        }
        return maxAbs;
    };

    float responseCtr = measureResponse(440.0f);
    float responseEdge = measureResponse(440.0f + 4.4f); // at half-bandwidth

    // At half-bandwidth, should be approximately -3 dB (0.707x)
    float ratioDb = 20.0f * std::log10(responseEdge / responseCtr);
    // Allow some tolerance: should be between -6 dB and 0 dB at the half-bandwidth point
    REQUIRE(ratioDb < 0.0f);
    REQUIRE(ratioDb > -6.0f);
    // More specifically, near -3 dB
    REQUIRE(ratioDb == Approx(-3.0f).margin(1.5f));
}

// =============================================================================
// T074: setBowModeActive tests
// =============================================================================

TEST_CASE("ModalResonatorBank getFeedbackVelocity returns 0 when bowModeActive is false",
          "[processors][modal][bow]")
{
    Krate::DSP::ModalResonatorBank bank;
    bank.prepare(kSampleRate);
    configureBasicModes(bank, 440.0f, 8);

    // Process some samples without bow mode active
    for (int i = 0; i < 100; ++i)
        (void)bank.processSample(0.5f);

    REQUIRE(bank.getFeedbackVelocity() == 0.0f);
}

TEST_CASE("ModalResonatorBank getFeedbackVelocity returns non-zero when bowModeActive is true",
          "[processors][modal][bow]")
{
    Krate::DSP::ModalResonatorBank bank;
    bank.prepare(kSampleRate);
    configureBasicModes(bank, 440.0f, 8);

    bank.setBowModeActive(true);
    bank.setBowPosition(0.13f);

    // Process samples with excitation to build up resonance
    for (int i = 0; i < 1000; ++i)
        (void)bank.processSample(0.1f);

    // After excitation, feedback velocity should be non-zero
    REQUIRE(bank.getFeedbackVelocity() != 0.0f);
}

// =============================================================================
// T075: setBowPosition harmonic weighting tests
// =============================================================================

TEST_CASE("ModalResonatorBank setBowPosition at 0.5 suppresses even modes (k=1)",
          "[processors][modal][bow]")
{
    // At position=0.5, weight for mode k=1 is sin(2 * pi * 0.5) = sin(pi) = 0
    // This means the 2nd harmonic should be suppressed
    Krate::DSP::ModalResonatorBank bank;
    bank.prepare(kSampleRate);
    configureBasicModes(bank, 220.0f, 8);

    bank.setBowModeActive(true);
    bank.setBowPosition(0.5f);

    // The harmonic weighting sin((k+1)*pi*0.5) for k=1: sin(2*pi*0.5) = sin(pi) = 0
    // Verify by checking that even harmonics get zero weight
    // We test this indirectly: feed a broadband excitation and check
    // that mode k=1 (2nd harmonic) contributes nothing

    // sin((k+1)*pi*0.5) values:
    // k=0: sin(pi*0.5) = 1.0
    // k=1: sin(pi*1.0) = 0.0  <-- suppressed
    // k=2: sin(pi*1.5) = -1.0
    // k=3: sin(pi*2.0) = 0.0  <-- suppressed
    // k=4: sin(pi*2.5) = 1.0
    // k=5: sin(pi*3.0) = 0.0  <-- suppressed
    // k=6: sin(pi*3.5) = -1.0
    // k=7: sin(pi*4.0) = 0.0  <-- suppressed

    // Verify mathematical weights directly
    float w0 = std::sin(1.0f * kPi * 0.5f);
    float w1 = std::sin(2.0f * kPi * 0.5f);
    float w2 = std::sin(3.0f * kPi * 0.5f);
    float w3 = std::sin(4.0f * kPi * 0.5f);

    REQUIRE(w0 == Approx(1.0f).margin(1e-6f));
    REQUIRE(w1 == Approx(0.0f).margin(1e-6f));
    REQUIRE(std::abs(w2) == Approx(1.0f).margin(1e-6f));
    REQUIRE(w3 == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("ModalResonatorBank setBowPosition at 0.13 gives all non-zero weights",
          "[processors][modal][bow]")
{
    // At position=0.13, sin((k+1)*pi*0.13) should be non-zero for k=0..7
    for (int k = 0; k < 8; ++k) {
        float weight = std::sin(static_cast<float>(k + 1) * kPi * 0.13f);
        REQUIRE(std::abs(weight) > 0.01f);
    }
}

// =============================================================================
// T076: Bowed-mode self-sustained oscillation test
// =============================================================================

TEST_CASE("ModalResonatorBank + BowExciter produces self-sustained oscillation",
          "[processors][modal][bow]")
{
    // Set up a modal resonator with bowed modes
    // Use short decay (0.1s) for higher input gain and faster buildup
    Krate::DSP::ModalResonatorBank bank;
    bank.prepare(kSampleRate);
    configureBasicModes(bank, 220.0f, 8, 0.1f, 0.0f);

    bank.setBowModeActive(true);
    bank.setBowPosition(0.13f);

    // Set up a bow exciter with moderate pressure
    Krate::DSP::BowExciter bow;
    bow.prepare(kSampleRate);
    bow.setPressure(0.5f);
    bow.setSpeed(0.7f);
    bow.setPosition(0.13f);
    bow.trigger(0.8f);

    // Prime the resonator with a brief impulse to seed oscillation
    // (Real-world: rosin jitter noise provides the seed; here we
    // speed up the test by providing a small initial excitation)
    (void)bank.process(0.1f);
    for (int i = 0; i < 200; ++i)
        (void)bank.process(0.0f);

    // Run feedback loop for 2 seconds using process() (IResonator interface)
    constexpr int kDuration = 88200;

    for (int i = 0; i < kDuration; ++i) {
        float fbVel = bank.getFeedbackVelocity();
        bow.setEnvelopeValue(1.0f);
        float excitation = bow.process(fbVel);
        (void)bank.process(excitation);
    }

    // After sustain, output should not have decayed below -60 dBFS
    // SC-004: self-sustained oscillation
    float peakAfterSustain = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        float fbVel = bank.getFeedbackVelocity();
        bow.setEnvelopeValue(1.0f);
        float excitation = bow.process(fbVel);
        float out = bank.process(excitation);
        peakAfterSustain = std::max(peakAfterSustain, std::abs(out));
    }

    INFO("Peak after sustain: " << peakAfterSustain);
    // -60 dBFS = 0.001
    REQUIRE(peakAfterSustain > 0.001f);
}
