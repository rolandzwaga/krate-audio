// ==============================================================================
// Decay Skew tests -- Phase 8, T103
// ==============================================================================
// Covers unnatural_zone_contract.md "Decay Skew" section and FR-051.
//
// Strategy for US6-2: Measure the Goertzel magnitude of the membrane
// fundamental and mode 7 at a "late" time window (100..400 ms). With
// decaySkew = -1, the mode 7 / fundamental ratio should be strictly larger
// than with decaySkew = 0. This is a weaker assertion than strict t60
// inversion but matches the physical limitation of the scalar-bias
// approximation (ModalResonatorBank has no per-mode decay API). The membrane
// mapper also applies a per-mode amplitude boost (research.md §9 escalation)
// so high modes start substantially louder at decaySkew = -1.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/drum_voice.h"
#include "dsp/bodies/membrane_mapper.h"

#include <allocation_detector.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int    kLengthSamples = static_cast<int>(0.5 * 44100); // 500 ms

inline bool isFiniteSample(float x) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

// Goertzel single-bin magnitude over [start, start+len).
double goertzelMag(const float* samples, int start, int len,
                   double sampleRate, double targetHz)
{
    const double omega = 2.0 * 3.14159265358979323846 * targetHz / sampleRate;
    const double coeff = 2.0 * std::cos(omega);
    double q0 = 0.0, q1 = 0.0, q2 = 0.0;
    for (int i = 0; i < len; ++i)
    {
        q0 = coeff * q1 - q2 + static_cast<double>(samples[start + i]);
        q2 = q1;
        q1 = q0;
    }
    const double mag2 = q1 * q1 + q2 * q2 - q1 * q2 * coeff;
    return std::sqrt(std::max(0.0, mag2));
}

std::vector<float> renderWithSkew(float decaySkew)
{
    Membrum::DrumVoice voice;
    voice.prepare(kSampleRate);
    voice.setMaterial(0.5f);
    voice.setSize(0.5f);
    voice.setDecay(0.8f);   // long decay so late-window magnitudes are measurable
    voice.setStrikePosition(0.3f);
    voice.setLevel(0.8f);
    voice.setExciterType(Membrum::ExciterType::Impulse);
    voice.setBodyModel(Membrum::BodyModelType::Membrane);
    voice.unnaturalZone().setModeStretch(1.0f);
    voice.unnaturalZone().setDecaySkew(decaySkew);

    voice.noteOn(0.8f);

    std::vector<float> out(static_cast<std::size_t>(kLengthSamples), 0.0f);
    for (int i = 0; i < kLengthSamples; ++i)
        out[static_cast<std::size_t>(i)] = voice.process();
    return out;
}

float rmsDb(const std::vector<float>& a, const std::vector<float>& b)
{
    REQUIRE(a.size() == b.size());
    double sumSq = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sumSq += d * d;
    }
    const double rms = std::sqrt(sumSq / static_cast<double>(a.size()));
    return (rms < 1e-12) ? -240.0f : static_cast<float>(20.0 * std::log10(rms));
}

} // namespace

// ==============================================================================
// T103(a) -- decaySkew = -1.0 biases the energy ratio toward higher modes.
// ==============================================================================

TEST_CASE("UnnaturalZone Decay Skew -- inversion shifts energy toward high modes",
          "[UnnaturalZone][DecaySkew]")
{
    // Reference frequencies: material=0.5 size=0.5 => f0 = 500 * 0.1^0.5.
    const float size = 0.5f;
    const float f0 = 500.0f * std::pow(0.1f, size);
    const double fundamentalHz =
        static_cast<double>(f0) * Membrum::kMembraneRatios[0];
    const double mode7Hz =
        static_cast<double>(f0) * Membrum::kMembraneRatios[7];

    const auto natural = renderWithSkew(0.0f);
    const auto inverted = renderWithSkew(-1.0f);

    const int winStart = static_cast<int>(0.1 * kSampleRate);
    const int winLen   = static_cast<int>(0.3 * kSampleRate);

    const double natF  = goertzelMag(natural.data(), winStart, winLen,
                                     kSampleRate, fundamentalHz);
    const double natH  = goertzelMag(natural.data(), winStart, winLen,
                                     kSampleRate, mode7Hz);
    const double invF  = goertzelMag(inverted.data(), winStart, winLen,
                                     kSampleRate, fundamentalHz);
    const double invH  = goertzelMag(inverted.data(), winStart, winLen,
                                     kSampleRate, mode7Hz);

    const double natRatio = (natF > 1e-9) ? natH / natF : 0.0;
    const double invRatio = (invF > 1e-9) ? invH / invF : 0.0;

    INFO("natural  -> fund=" << natF << " mode7=" << natH
                             << " ratio=" << natRatio);
    INFO("inverted -> fund=" << invF << " mode7=" << invH
                             << " ratio=" << invRatio);

    // With decaySkew=-1, the mode7/fundamental ratio must be strictly larger
    // than with decaySkew=0 (scalar-bias approximation's best achievable
    // inversion, research.md §9).
    CHECK(invRatio > natRatio);
}

// ==============================================================================
// T103(b) -- decaySkew == 0.0 must produce bit-identical DrumVoice output
//            compared to the "Unnatural Zone disabled" path (FR-055).
// ==============================================================================

TEST_CASE("UnnaturalZone Decay Skew -- decaySkew==0 matches defaults-off path",
          "[UnnaturalZone][DecaySkew][DefaultsOff]")
{
    const auto withZeroSkew = renderWithSkew(0.0f);

    Membrum::DrumVoice voice;
    voice.prepare(kSampleRate);
    voice.setMaterial(0.5f);
    voice.setSize(0.5f);
    voice.setDecay(0.8f);
    voice.setStrikePosition(0.3f);
    voice.setLevel(0.8f);
    voice.setExciterType(Membrum::ExciterType::Impulse);
    voice.setBodyModel(Membrum::BodyModelType::Membrane);
    voice.noteOn(0.8f);

    std::vector<float> baseline(static_cast<std::size_t>(kLengthSamples), 0.0f);
    for (int i = 0; i < kLengthSamples; ++i)
        baseline[static_cast<std::size_t>(i)] = voice.process();

    const float db = rmsDb(baseline, withZeroSkew);
    INFO("RMS difference (zero-skew vs untouched) = " << db << " dBFS");
    CHECK(db <= -120.0f);
}

// ==============================================================================
// T103(c) -- Allocation detector: zero heap activity for decaySkew path.
// ==============================================================================

TEST_CASE("UnnaturalZone Decay Skew -- zero heap allocations on audio thread",
          "[UnnaturalZone][DecaySkew][allocation]")
{
    Membrum::DrumVoice voice;
    voice.prepare(kSampleRate);
    voice.setMaterial(0.5f);
    voice.setSize(0.5f);
    voice.setDecay(0.5f);
    voice.setStrikePosition(0.3f);
    voice.setLevel(0.8f);
    voice.setExciterType(Membrum::ExciterType::Impulse);
    voice.setBodyModel(Membrum::BodyModelType::Membrane);

    voice.noteOn(0.8f);
    std::array<float, 64> drain{};
    for (int i = 0; i < 8; ++i)
        voice.processBlock(drain.data(), 64);
    voice.noteOff();

    {
        TestHelpers::AllocationScope scope;
        voice.unnaturalZone().setDecaySkew(-1.0f);
        voice.noteOn(0.8f);
        std::array<float, 512> block{};
        voice.processBlock(block.data(), 512);
        voice.noteOff();
        const size_t count = scope.getAllocationCount();
        INFO("Decay Skew path alloc count = " << count);
        CHECK(count == 0u);
    }
}
