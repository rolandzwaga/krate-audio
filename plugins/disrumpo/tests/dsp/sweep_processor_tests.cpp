// ==============================================================================
// Tests: SweepProcessor (User Story 1)
// ==============================================================================
// Unit tests for the core SweepProcessor DSP class.
//
// Reference: specs/007-sweep-system/spec.md (FR-001 through FR-010)
// Reference: specs/007-sweep-system/data-model.md (SweepProcessor entity)
// ==============================================================================

#include "dsp/sweep_processor.h"
#include "dsp/sweep_types.h"
#include "plugin_ids.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

using Catch::Approx;
using namespace Disrumpo;

// ==============================================================================
// Construction and Preparation Tests
// ==============================================================================

TEST_CASE("SweepProcessor: construction", "[sweep][processor]") {
    SweepProcessor proc;

    SECTION("has default values") {
        REQUIRE_FALSE(proc.isEnabled());
        REQUIRE(proc.getTargetFrequency() == Approx(kDefaultSweepFreqHz));
        REQUIRE(proc.getWidth() == Approx(kDefaultSweepWidth));
        REQUIRE(proc.getIntensity() == Approx(kDefaultIntensity));
        REQUIRE(proc.getFalloffMode() == SweepFalloff::Smooth);
        REQUIRE(proc.getMorphLinkMode() == MorphLinkMode::None);
    }
}

TEST_CASE("SweepProcessor: prepare", "[sweep][processor]") {
    SweepProcessor proc;

    SECTION("prepare accepts sample rate") {
        proc.prepare(44100.0, 512);
        // Should not crash and be ready for processing
    }

    SECTION("prepare with different sample rates") {
        proc.prepare(48000.0, 256);
        proc.prepare(96000.0, 1024);
    }
}

TEST_CASE("SweepProcessor: reset", "[sweep][processor]") {
    SweepProcessor proc;
    proc.prepare(44100.0, 512);

    // Modify state
    proc.setEnabled(true);
    proc.setCenterFrequency(5000.0f);

    // Reset
    proc.reset();

    // Smoother should reset (frequency snaps to target)
    REQUIRE(proc.getSmoothedFrequency() == Approx(proc.getTargetFrequency()).margin(1.0f));
}

// ==============================================================================
// Enable/Disable Tests (FR-011, FR-012, FR-013)
// ==============================================================================

TEST_CASE("SweepProcessor: enable/disable", "[sweep][processor]") {
    SweepProcessor proc;
    proc.prepare(44100.0, 512);

    SECTION("disabled by default") {
        REQUIRE_FALSE(proc.isEnabled());
    }

    SECTION("can enable") {
        proc.setEnabled(true);
        REQUIRE(proc.isEnabled());
    }

    SECTION("can disable") {
        proc.setEnabled(true);
        proc.setEnabled(false);
        REQUIRE_FALSE(proc.isEnabled());
    }
}

// ==============================================================================
// Parameter Setter Tests (FR-002 through FR-007)
// ==============================================================================

TEST_CASE("SweepProcessor: frequency parameter (FR-002)", "[sweep][processor]") {
    SweepProcessor proc;
    proc.prepare(44100.0, 512);

    SECTION("accepts valid frequencies") {
        proc.setCenterFrequency(440.0f);
        REQUIRE(proc.getTargetFrequency() == Approx(440.0f));

        proc.setCenterFrequency(1000.0f);
        REQUIRE(proc.getTargetFrequency() == Approx(1000.0f));
    }

    SECTION("clamps to valid range [20, 20000]") {
        proc.setCenterFrequency(10.0f);  // Below minimum
        REQUIRE(proc.getTargetFrequency() >= kMinSweepFreqHz);

        proc.setCenterFrequency(30000.0f);  // Above maximum
        REQUIRE(proc.getTargetFrequency() <= kMaxSweepFreqHz);
    }
}

TEST_CASE("SweepProcessor: width parameter (FR-003)", "[sweep][processor]") {
    SweepProcessor proc;
    proc.prepare(44100.0, 512);

    SECTION("accepts valid widths") {
        proc.setWidth(1.0f);
        REQUIRE(proc.getWidth() == Approx(1.0f));

        proc.setWidth(3.0f);
        REQUIRE(proc.getWidth() == Approx(3.0f));
    }

    SECTION("clamps to valid range [0.5, 4.0]") {
        proc.setWidth(0.1f);  // Below minimum
        REQUIRE(proc.getWidth() >= kMinSweepWidth);

        proc.setWidth(10.0f);  // Above maximum
        REQUIRE(proc.getWidth() <= kMaxSweepWidth);
    }
}

TEST_CASE("SweepProcessor: intensity parameter (FR-004)", "[sweep][processor]") {
    SweepProcessor proc;
    proc.prepare(44100.0, 512);

    SECTION("accepts valid intensities") {
        proc.setIntensity(0.5f);
        REQUIRE(proc.getIntensity() == Approx(0.5f));

        proc.setIntensity(1.5f);  // 150%
        REQUIRE(proc.getIntensity() == Approx(1.5f));
    }

    SECTION("clamps to valid range [0, 2]") {
        proc.setIntensity(-0.5f);  // Below minimum
        REQUIRE(proc.getIntensity() >= 0.0f);

        proc.setIntensity(3.0f);  // Above 200%
        REQUIRE(proc.getIntensity() <= kMaxIntensity);
    }
}

TEST_CASE("SweepProcessor: falloff mode (FR-005)", "[sweep][processor]") {
    SweepProcessor proc;

    SECTION("default is Smooth") {
        REQUIRE(proc.getFalloffMode() == SweepFalloff::Smooth);
    }

    SECTION("can set to Sharp") {
        proc.setFalloffMode(SweepFalloff::Sharp);
        REQUIRE(proc.getFalloffMode() == SweepFalloff::Sharp);
    }

    SECTION("can set to Smooth") {
        proc.setFalloffMode(SweepFalloff::Sharp);
        proc.setFalloffMode(SweepFalloff::Smooth);
        REQUIRE(proc.getFalloffMode() == SweepFalloff::Smooth);
    }
}

TEST_CASE("SweepProcessor: morph link mode (FR-014)", "[sweep][processor]") {
    SweepProcessor proc;

    SECTION("default is None") {
        REQUIRE(proc.getMorphLinkMode() == MorphLinkMode::None);
    }

    SECTION("can set all modes") {
        proc.setMorphLinkMode(MorphLinkMode::SweepFreq);
        REQUIRE(proc.getMorphLinkMode() == MorphLinkMode::SweepFreq);

        proc.setMorphLinkMode(MorphLinkMode::InverseSweep);
        REQUIRE(proc.getMorphLinkMode() == MorphLinkMode::InverseSweep);

        proc.setMorphLinkMode(MorphLinkMode::Custom);
        REQUIRE(proc.getMorphLinkMode() == MorphLinkMode::Custom);
    }
}

// ==============================================================================
// Frequency Smoothing Tests (FR-007a)
// ==============================================================================

TEST_CASE("SweepProcessor: frequency smoothing", "[sweep][processor]") {
    SweepProcessor proc;
    proc.prepare(44100.0, 512);
    proc.setEnabled(true);

    SECTION("setCenterFrequency targets smoother") {
        proc.setCenterFrequency(1000.0f);
        REQUIRE(proc.getTargetFrequency() == Approx(1000.0f));

        // After setting, target should be set but smoothed value may lag
        proc.setCenterFrequency(2000.0f);
        REQUIRE(proc.getTargetFrequency() == Approx(2000.0f));
    }

    SECTION("process advances smoother") {
        proc.setCenterFrequency(100.0f);
        proc.reset();  // Snap to 100 Hz

        proc.setCenterFrequency(10000.0f);  // Big jump

        float initial = proc.getSmoothedFrequency();

        // Process many samples to advance smoother
        for (int i = 0; i < 1000; ++i) {
            proc.process();
        }

        float after = proc.getSmoothedFrequency();

        // After processing, smoothed value should have moved toward target
        REQUIRE(after > initial);
        REQUIRE(after <= 10000.0f);
    }

    SECTION("smoothing time affects transition speed") {
        proc.setSmoothingTime(50.0f);  // 50ms - slower

        proc.setCenterFrequency(100.0f);
        proc.reset();

        proc.setCenterFrequency(10000.0f);

        // After 20ms worth of samples (882 samples at 44.1kHz)
        for (int i = 0; i < 882; ++i) {
            proc.process();
        }

        float at20ms = proc.getSmoothedFrequency();

        // Should not have fully reached target yet with 50ms smoothing
        REQUIRE(at20ms < 10000.0f);
    }
}

// ==============================================================================
// Band Intensity Calculation Tests (FR-008, FR-009, FR-010)
// ==============================================================================

TEST_CASE("SweepProcessor: calculateBandIntensity", "[sweep][processor]") {
    SweepProcessor proc;
    proc.prepare(44100.0, 512);
    proc.setEnabled(true);
    proc.setCenterFrequency(1000.0f);
    proc.setWidth(2.0f);  // 2 octave width
    proc.setIntensity(1.0f);  // 100%
    proc.reset();  // Snap smoother to 1000 Hz

    SECTION("Gaussian mode: center band has full intensity") {
        proc.setFalloffMode(SweepFalloff::Smooth);
        float result = proc.calculateBandIntensity(1000.0f);
        REQUIRE(result == Approx(1.0f).margin(0.01f));
    }

    SECTION("Gaussian mode: 1 octave away has ~0.606 intensity") {
        proc.setFalloffMode(SweepFalloff::Smooth);
        float result = proc.calculateBandIntensity(2000.0f);  // 1 octave above
        REQUIRE(result == Approx(0.606f).margin(0.02f));
    }

    SECTION("Sharp mode: center band has full intensity") {
        proc.setFalloffMode(SweepFalloff::Sharp);
        float result = proc.calculateBandIntensity(1000.0f);
        REQUIRE(result == Approx(1.0f).margin(0.01f));
    }

    SECTION("Sharp mode: edge has zero intensity") {
        proc.setFalloffMode(SweepFalloff::Sharp);
        float result = proc.calculateBandIntensity(2000.0f);  // At edge (1 octave = width/2)
        REQUIRE(result == Approx(0.0f).margin(0.01f));
    }

    SECTION("Sharp mode: beyond edge has zero intensity") {
        proc.setFalloffMode(SweepFalloff::Sharp);
        float result = proc.calculateBandIntensity(4000.0f);  // Beyond edge
        REQUIRE(result == Approx(0.0f).margin(0.01f));
    }
}

TEST_CASE("SweepProcessor: calculateAllBandIntensities", "[sweep][processor]") {
    SweepProcessor proc;
    proc.prepare(44100.0, 512);
    proc.setEnabled(true);
    proc.setCenterFrequency(1000.0f);
    proc.setWidth(2.0f);
    proc.setIntensity(1.0f);
    proc.reset();

    // Band centers spanning 100 Hz to 10 kHz
    std::array<float, 8> bandCenters = {100.0f, 200.0f, 400.0f, 800.0f,
                                         1600.0f, 3200.0f, 6400.0f, 12800.0f};
    std::array<float, 8> intensities{};

    proc.calculateAllBandIntensities(bandCenters.data(), 8, intensities.data());

    SECTION("all intensities are valid (non-negative)") {
        for (float intensity : intensities) {
            REQUIRE(intensity >= 0.0f);
        }
    }

    SECTION("bands near center have higher intensity") {
        // 800 Hz and 1600 Hz are closest to 1000 Hz center
        // Their intensities should be highest
        float sumNear = intensities[3] + intensities[4];  // 800 + 1600 Hz
        float sumFar = intensities[0] + intensities[7];   // 100 + 12800 Hz
        REQUIRE(sumNear > sumFar);
    }
}

// ==============================================================================
// Morph Position Linking Tests (FR-014 to FR-022)
// ==============================================================================

TEST_CASE("SweepProcessor: getMorphPosition", "[sweep][processor]") {
    SweepProcessor proc;
    proc.prepare(44100.0, 512);
    proc.setEnabled(true);
    proc.setWidth(2.0f);
    proc.setIntensity(1.0f);

    SECTION("None mode returns 0.5 (center)") {
        proc.setMorphLinkMode(MorphLinkMode::None);
        proc.setCenterFrequency(1000.0f);
        proc.reset();
        REQUIRE(proc.getMorphPosition() == Approx(0.5f).margin(0.01f));
    }

    SECTION("Linear mode maps frequency to position") {
        proc.setMorphLinkMode(MorphLinkMode::SweepFreq);

        // Low frequency -> low position
        proc.setCenterFrequency(20.0f);
        proc.reset();
        REQUIRE(proc.getMorphPosition() == Approx(0.0f).margin(0.01f));

        // High frequency -> high position
        proc.setCenterFrequency(20000.0f);
        proc.reset();
        REQUIRE(proc.getMorphPosition() == Approx(1.0f).margin(0.01f));
    }

    SECTION("Inverse mode inverts frequency mapping") {
        proc.setMorphLinkMode(MorphLinkMode::InverseSweep);

        // Low frequency -> high position
        proc.setCenterFrequency(20.0f);
        proc.reset();
        REQUIRE(proc.getMorphPosition() == Approx(1.0f).margin(0.01f));

        // High frequency -> low position
        proc.setCenterFrequency(20000.0f);
        proc.reset();
        REQUIRE(proc.getMorphPosition() == Approx(0.0f).margin(0.01f));
    }
}

// ==============================================================================
// Position Data for UI Sync (FR-046)
// ==============================================================================

TEST_CASE("SweepProcessor: getPositionData", "[sweep][processor]") {
    SweepProcessor proc;
    proc.prepare(44100.0, 512);
    proc.setEnabled(true);
    proc.setCenterFrequency(1500.0f);
    proc.setWidth(2.5f);
    proc.setIntensity(0.75f);
    proc.setFalloffMode(SweepFalloff::Sharp);
    proc.reset();

    auto data = proc.getPositionData(12345);

    SECTION("position data reflects current state") {
        REQUIRE(data.centerFreqHz == Approx(1500.0f).margin(10.0f));
        REQUIRE(data.widthOctaves == Approx(2.5f));
        REQUIRE(data.intensity == Approx(0.75f));
        REQUIRE(data.samplePosition == 12345);
        REQUIRE(data.enabled == true);
        REQUIRE(data.falloff == static_cast<uint8_t>(SweepFalloff::Sharp));
    }
}

// ==============================================================================
// Disabled State Tests (FR-011, FR-012, FR-013)
// ==============================================================================

TEST_CASE("SweepProcessor: disabled state behavior", "[sweep][processor]") {
    SweepProcessor proc;
    proc.prepare(44100.0, 512);
    proc.setCenterFrequency(1000.0f);
    proc.setWidth(2.0f);
    proc.setIntensity(1.0f);
    proc.reset();

    // Keep disabled
    proc.setEnabled(false);

    SECTION("disabled processor returns zero intensity") {
        float result = proc.calculateBandIntensity(1000.0f);
        REQUIRE(result == Approx(0.0f).margin(0.001f));
    }

    SECTION("disabled processor returns center morph position") {
        proc.setMorphLinkMode(MorphLinkMode::SweepFreq);
        REQUIRE(proc.getMorphPosition() == Approx(0.5f).margin(0.01f));
    }

    SECTION("disabled processor position data shows disabled") {
        auto data = proc.getPositionData(0);
        REQUIRE(data.enabled == false);
    }
}
