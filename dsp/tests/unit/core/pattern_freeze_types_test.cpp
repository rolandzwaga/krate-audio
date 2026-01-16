// ==============================================================================
// Layer 0: Core Utility Tests - Pattern Freeze Types
// ==============================================================================
// Unit tests for Pattern Freeze type definitions (spec 069).
//
// Constitution Compliance:
// - Principle VIII: Testing Discipline
// - Test-first development methodology
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/core/pattern_freeze_types.h>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// PatternType Tests
// =============================================================================

TEST_CASE("PatternType enum has correct values", "[core][pattern_freeze][layer0]") {
    REQUIRE(static_cast<uint8_t>(PatternType::Euclidean) == 0);
    REQUIRE(static_cast<uint8_t>(PatternType::GranularScatter) == 1);
    REQUIRE(static_cast<uint8_t>(PatternType::HarmonicDrones) == 2);
    REQUIRE(static_cast<uint8_t>(PatternType::NoiseBursts) == 3);
    REQUIRE(kPatternTypeCount == 4);
}

TEST_CASE("PatternType defaults to Euclidean", "[core][pattern_freeze][layer0]") {
    REQUIRE(kDefaultPatternType == PatternType::Euclidean);
}

TEST_CASE("getPatternTypeName returns correct names", "[core][pattern_freeze][layer0]") {
    REQUIRE(std::string(getPatternTypeName(PatternType::Euclidean)) == "Euclidean");
    REQUIRE(std::string(getPatternTypeName(PatternType::GranularScatter)) == "Granular Scatter");
    REQUIRE(std::string(getPatternTypeName(PatternType::HarmonicDrones)) == "Harmonic Drones");
    REQUIRE(std::string(getPatternTypeName(PatternType::NoiseBursts)) == "Noise Bursts");
}

TEST_CASE("getPatternTypeName handles invalid values", "[core][pattern_freeze][layer0][edge]") {
    // Cast an out-of-range value
    auto invalid = static_cast<PatternType>(255);
    REQUIRE(std::string(getPatternTypeName(invalid)) == "Unknown");
}

// =============================================================================
// SliceMode Tests
// =============================================================================

TEST_CASE("SliceMode enum has correct values", "[core][pattern_freeze][layer0]") {
    REQUIRE(static_cast<uint8_t>(SliceMode::Fixed) == 0);
    REQUIRE(static_cast<uint8_t>(SliceMode::Variable) == 1);
    REQUIRE(kSliceModeCount == 2);
    REQUIRE(kDefaultSliceMode == SliceMode::Fixed);
}

TEST_CASE("getSliceModeName returns correct names", "[core][pattern_freeze][layer0]") {
    REQUIRE(std::string(getSliceModeName(SliceMode::Fixed)) == "Fixed");
    REQUIRE(std::string(getSliceModeName(SliceMode::Variable)) == "Variable");
}

TEST_CASE("getSliceModeName handles invalid values", "[core][pattern_freeze][layer0][edge]") {
    auto invalid = static_cast<SliceMode>(255);
    REQUIRE(std::string(getSliceModeName(invalid)) == "Unknown");
}

// =============================================================================
// PitchInterval Tests
// =============================================================================

TEST_CASE("PitchInterval enum has correct values", "[core][pattern_freeze][layer0]") {
    REQUIRE(static_cast<uint8_t>(PitchInterval::Unison) == 0);
    REQUIRE(static_cast<uint8_t>(PitchInterval::MinorThird) == 1);
    REQUIRE(static_cast<uint8_t>(PitchInterval::MajorThird) == 2);
    REQUIRE(static_cast<uint8_t>(PitchInterval::Fourth) == 3);
    REQUIRE(static_cast<uint8_t>(PitchInterval::Fifth) == 4);
    REQUIRE(static_cast<uint8_t>(PitchInterval::Octave) == 5);
    REQUIRE(kPitchIntervalCount == 6);
    REQUIRE(kDefaultPitchInterval == PitchInterval::Octave);
}

TEST_CASE("getIntervalSemitones returns correct semitone offsets", "[core][pattern_freeze][layer0]") {
    REQUIRE(getIntervalSemitones(PitchInterval::Unison) == Approx(0.0f));
    REQUIRE(getIntervalSemitones(PitchInterval::MinorThird) == Approx(3.0f));
    REQUIRE(getIntervalSemitones(PitchInterval::MajorThird) == Approx(4.0f));
    REQUIRE(getIntervalSemitones(PitchInterval::Fourth) == Approx(5.0f));
    REQUIRE(getIntervalSemitones(PitchInterval::Fifth) == Approx(7.0f));
    REQUIRE(getIntervalSemitones(PitchInterval::Octave) == Approx(12.0f));
}

TEST_CASE("getIntervalSemitones handles invalid values", "[core][pattern_freeze][layer0][edge]") {
    auto invalid = static_cast<PitchInterval>(255);
    REQUIRE(getIntervalSemitones(invalid) == Approx(0.0f));
}

TEST_CASE("getPitchIntervalName returns correct names", "[core][pattern_freeze][layer0]") {
    REQUIRE(std::string(getPitchIntervalName(PitchInterval::Unison)) == "Unison");
    REQUIRE(std::string(getPitchIntervalName(PitchInterval::MinorThird)) == "Minor 3rd");
    REQUIRE(std::string(getPitchIntervalName(PitchInterval::MajorThird)) == "Major 3rd");
    REQUIRE(std::string(getPitchIntervalName(PitchInterval::Fourth)) == "Perfect 4th");
    REQUIRE(std::string(getPitchIntervalName(PitchInterval::Fifth)) == "Perfect 5th");
    REQUIRE(std::string(getPitchIntervalName(PitchInterval::Octave)) == "Octave");
}

// =============================================================================
// NoiseColor Tests
// =============================================================================

TEST_CASE("NoiseColor enum has correct values", "[core][pattern_freeze][layer0]") {
    REQUIRE(static_cast<uint8_t>(NoiseColor::White) == 0);
    REQUIRE(static_cast<uint8_t>(NoiseColor::Pink) == 1);
    REQUIRE(static_cast<uint8_t>(NoiseColor::Brown) == 2);
    REQUIRE(static_cast<uint8_t>(NoiseColor::Blue) == 3);
    REQUIRE(static_cast<uint8_t>(NoiseColor::Violet) == 4);
    REQUIRE(static_cast<uint8_t>(NoiseColor::Grey) == 5);
    REQUIRE(static_cast<uint8_t>(NoiseColor::Velvet) == 6);
    REQUIRE(static_cast<uint8_t>(NoiseColor::RadioStatic) == 7);
    REQUIRE(kNoiseColorCount == 8);
    REQUIRE(kDefaultNoiseColor == NoiseColor::Pink);
}

TEST_CASE("getNoiseColorName returns correct names", "[core][pattern_freeze][layer0]") {
    REQUIRE(std::string(getNoiseColorName(NoiseColor::White)) == "White");
    REQUIRE(std::string(getNoiseColorName(NoiseColor::Pink)) == "Pink");
    REQUIRE(std::string(getNoiseColorName(NoiseColor::Brown)) == "Brown");
    REQUIRE(std::string(getNoiseColorName(NoiseColor::Blue)) == "Blue");
    REQUIRE(std::string(getNoiseColorName(NoiseColor::Violet)) == "Violet");
    REQUIRE(std::string(getNoiseColorName(NoiseColor::Grey)) == "Grey");
    REQUIRE(std::string(getNoiseColorName(NoiseColor::Velvet)) == "Velvet");
    REQUIRE(std::string(getNoiseColorName(NoiseColor::RadioStatic)) == "Radio");
}

TEST_CASE("getNoiseColorName handles invalid values", "[core][pattern_freeze][layer0][edge]") {
    auto invalid = static_cast<NoiseColor>(255);
    REQUIRE(std::string(getNoiseColorName(invalid)) == "Unknown");
}

// =============================================================================
// EnvelopeShape Tests
// =============================================================================

TEST_CASE("EnvelopeShape enum has correct values", "[core][pattern_freeze][layer0]") {
    REQUIRE(static_cast<uint8_t>(EnvelopeShape::Linear) == 0);
    REQUIRE(static_cast<uint8_t>(EnvelopeShape::Exponential) == 1);
    REQUIRE(kEnvelopeShapeCount == 2);
    REQUIRE(kDefaultEnvelopeShape == EnvelopeShape::Linear);
}

TEST_CASE("getEnvelopeShapeName returns correct names", "[core][pattern_freeze][layer0]") {
    REQUIRE(std::string(getEnvelopeShapeName(EnvelopeShape::Linear)) == "Linear");
    REQUIRE(std::string(getEnvelopeShapeName(EnvelopeShape::Exponential)) == "Exponential");
}

TEST_CASE("getEnvelopeShapeName handles invalid values", "[core][pattern_freeze][layer0][edge]") {
    auto invalid = static_cast<EnvelopeShape>(255);
    REQUIRE(std::string(getEnvelopeShapeName(invalid)) == "Unknown");
}

// =============================================================================
// PatternFreezeConstants Tests
// =============================================================================

TEST_CASE("PatternFreezeConstants have valid ranges", "[core][pattern_freeze][layer0]") {
    using namespace PatternFreezeConstants;

    SECTION("Slice length constants are valid") {
        REQUIRE(kMinSliceLengthMs > 0.0f);
        REQUIRE(kMaxSliceLengthMs > kMinSliceLengthMs);
        REQUIRE(kDefaultSliceLengthMs >= kMinSliceLengthMs);
        REQUIRE(kDefaultSliceLengthMs <= kMaxSliceLengthMs);
    }

    SECTION("Euclidean constants are valid") {
        REQUIRE(kMinEuclideanSteps >= 2);
        REQUIRE(kMaxEuclideanSteps >= kMinEuclideanSteps);
        REQUIRE(kDefaultEuclideanSteps >= kMinEuclideanSteps);
        REQUIRE(kDefaultEuclideanSteps <= kMaxEuclideanSteps);
        REQUIRE(kDefaultEuclideanHits >= 1);
        REQUIRE(kDefaultEuclideanHits <= kDefaultEuclideanSteps);
        REQUIRE(kDefaultEuclideanRotation >= 0);
    }

    SECTION("Granular constants are valid") {
        REQUIRE(kMinGranularDensityHz > 0.0f);
        REQUIRE(kMaxGranularDensityHz > kMinGranularDensityHz);
        REQUIRE(kDefaultGranularDensityHz >= kMinGranularDensityHz);
        REQUIRE(kDefaultGranularDensityHz <= kMaxGranularDensityHz);
        REQUIRE(kMinGranularGrainSizeMs > 0.0f);
        REQUIRE(kMaxGranularGrainSizeMs > kMinGranularGrainSizeMs);
        REQUIRE(kDefaultGranularGrainSizeMs >= kMinGranularGrainSizeMs);
        REQUIRE(kDefaultGranularGrainSizeMs <= kMaxGranularGrainSizeMs);
        REQUIRE(kDefaultPositionJitter >= 0.0f);
        REQUIRE(kDefaultPositionJitter <= 1.0f);
        REQUIRE(kDefaultSizeJitter >= 0.0f);
        REQUIRE(kDefaultSizeJitter <= 1.0f);
    }

    SECTION("Drone constants are valid") {
        REQUIRE(kMinDroneVoices >= 1);
        REQUIRE(kMaxDroneVoices >= kMinDroneVoices);
        REQUIRE(kDefaultDroneVoices >= kMinDroneVoices);
        REQUIRE(kDefaultDroneVoices <= kMaxDroneVoices);
        REQUIRE(kMinDroneDriftRateHz > 0.0f);
        REQUIRE(kMaxDroneDriftRateHz > kMinDroneDriftRateHz);
        REQUIRE(kDefaultDroneDriftRateHz >= kMinDroneDriftRateHz);
        REQUIRE(kDefaultDroneDriftRateHz <= kMaxDroneDriftRateHz);
        REQUIRE(kDefaultDroneDrift >= 0.0f);
        REQUIRE(kDefaultDroneDrift <= 1.0f);
    }

    SECTION("Noise filter constants are valid") {
        REQUIRE(kMinNoiseFilterCutoffHz >= 20.0f);
        REQUIRE(kMaxNoiseFilterCutoffHz <= 20000.0f);
        REQUIRE(kDefaultNoiseFilterCutoffHz >= kMinNoiseFilterCutoffHz);
        REQUIRE(kDefaultNoiseFilterCutoffHz <= kMaxNoiseFilterCutoffHz);
        REQUIRE(kDefaultNoiseFilterSweep >= 0.0f);
        REQUIRE(kDefaultNoiseFilterSweep <= 1.0f);
    }

    SECTION("Envelope constants are valid") {
        REQUIRE(kMinEnvelopeAttackMs >= 0.0f);
        REQUIRE(kMaxEnvelopeAttackMs > kMinEnvelopeAttackMs);
        REQUIRE(kDefaultEnvelopeAttackMs >= kMinEnvelopeAttackMs);
        REQUIRE(kDefaultEnvelopeAttackMs <= kMaxEnvelopeAttackMs);
        REQUIRE(kMinEnvelopeReleaseMs >= 0.0f);
        REQUIRE(kMaxEnvelopeReleaseMs > kMinEnvelopeReleaseMs);
        REQUIRE(kDefaultEnvelopeReleaseMs >= kMinEnvelopeReleaseMs);
        REQUIRE(kDefaultEnvelopeReleaseMs <= kMaxEnvelopeReleaseMs);
    }

    SECTION("Capture buffer constants are valid") {
        REQUIRE(kMinCaptureBufferSeconds > 0.0f);
        REQUIRE(kMaxCaptureBufferSeconds >= kMinCaptureBufferSeconds);
        REQUIRE(kDefaultCaptureBufferSeconds >= kMinCaptureBufferSeconds);
        REQUIRE(kDefaultCaptureBufferSeconds <= kMaxCaptureBufferSeconds);
        REQUIRE(kMinReadyBufferMs > 0.0f);
    }

    SECTION("Pattern crossfade is positive") {
        REQUIRE(kPatternCrossfadeMs > 0.0f);
    }

    SECTION("Max slices is reasonable") {
        REQUIRE(kMaxSlices >= 1);
        REQUIRE(kMaxSlices <= 16);  // Upper bound for sanity
    }
}
