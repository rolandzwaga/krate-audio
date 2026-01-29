// ==============================================================================
// Tests: SweepPositionBuffer
// ==============================================================================
// Lock-free SPSC ring buffer tests for audio-UI sweep position synchronization.
//
// Reference: specs/007-sweep-system/spec.md (FR-046, FR-047)
// ==============================================================================

#include <krate/dsp/primitives/sweep_position_buffer.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using Catch::Approx;
using namespace Krate::DSP;

TEST_CASE("SweepPositionBuffer: default construction", "[sweep][buffer][primitives]") {
    SweepPositionBuffer buffer;

    SECTION("buffer starts empty") {
        REQUIRE(buffer.isEmpty());
        REQUIRE(buffer.count() == 0);
    }

    SECTION("pop from empty buffer returns false") {
        SweepPositionData data;
        REQUIRE_FALSE(buffer.pop(data));
    }

    SECTION("getLatest from empty buffer returns false") {
        SweepPositionData data;
        REQUIRE_FALSE(buffer.getLatest(data));
    }
}

TEST_CASE("SweepPositionBuffer: push and pop", "[sweep][buffer][primitives]") {
    SweepPositionBuffer buffer;

    SECTION("push single entry") {
        SweepPositionData data;
        data.centerFreqHz = 1000.0f;
        data.widthOctaves = 2.0f;
        data.intensity = 0.75f;
        data.samplePosition = 12345;
        data.enabled = true;
        data.falloff = 1;  // Smooth

        REQUIRE(buffer.push(data));
        REQUIRE_FALSE(buffer.isEmpty());
        REQUIRE(buffer.count() == 1);
    }

    SECTION("pop retrieves pushed data") {
        SweepPositionData input;
        input.centerFreqHz = 440.0f;
        input.widthOctaves = 1.5f;
        input.intensity = 0.5f;
        input.samplePosition = 9999;
        input.enabled = true;
        input.falloff = 0;  // Sharp

        buffer.push(input);

        SweepPositionData output;
        REQUIRE(buffer.pop(output));

        REQUIRE(output.centerFreqHz == Approx(440.0f));
        REQUIRE(output.widthOctaves == Approx(1.5f));
        REQUIRE(output.intensity == Approx(0.5f));
        REQUIRE(output.samplePosition == 9999);
        REQUIRE(output.enabled == true);
        REQUIRE(output.falloff == 0);
    }

    SECTION("FIFO order preserved") {
        // Push 3 entries with different frequencies
        for (int i = 0; i < 3; ++i) {
            SweepPositionData data;
            data.centerFreqHz = static_cast<float>(100 * (i + 1));  // 100, 200, 300
            data.samplePosition = static_cast<uint64_t>(i);
            buffer.push(data);
        }

        REQUIRE(buffer.count() == 3);

        // Pop should return in FIFO order
        SweepPositionData out;
        REQUIRE(buffer.pop(out));
        REQUIRE(out.centerFreqHz == Approx(100.0f));

        REQUIRE(buffer.pop(out));
        REQUIRE(out.centerFreqHz == Approx(200.0f));

        REQUIRE(buffer.pop(out));
        REQUIRE(out.centerFreqHz == Approx(300.0f));

        REQUIRE(buffer.isEmpty());
    }
}

TEST_CASE("SweepPositionBuffer: buffer full behavior", "[sweep][buffer][primitives]") {
    SweepPositionBuffer buffer;

    SECTION("push returns false when buffer full") {
        // Fill buffer to capacity (kSweepBufferSize = 8)
        for (int i = 0; i < kSweepBufferSize; ++i) {
            SweepPositionData data;
            data.centerFreqHz = static_cast<float>(i * 100);
            REQUIRE(buffer.push(data));
        }

        REQUIRE(buffer.count() == kSweepBufferSize);

        // Next push should fail (buffer full)
        SweepPositionData overflow;
        overflow.centerFreqHz = 9999.0f;
        REQUIRE_FALSE(buffer.push(overflow));

        // Count should remain at max
        REQUIRE(buffer.count() == kSweepBufferSize);
    }

    SECTION("can push after popping from full buffer") {
        // Fill buffer
        for (int i = 0; i < kSweepBufferSize; ++i) {
            SweepPositionData data;
            buffer.push(data);
        }

        // Pop one
        SweepPositionData temp;
        buffer.pop(temp);

        // Now we should be able to push again
        SweepPositionData newData;
        newData.centerFreqHz = 5000.0f;
        REQUIRE(buffer.push(newData));
    }
}

TEST_CASE("SweepPositionBuffer: getLatest", "[sweep][buffer][primitives]") {
    SweepPositionBuffer buffer;

    SECTION("getLatest returns newest entry without removing") {
        SweepPositionData d1, d2, d3;
        d1.centerFreqHz = 100.0f;
        d2.centerFreqHz = 200.0f;
        d3.centerFreqHz = 300.0f;

        buffer.push(d1);
        buffer.push(d2);
        buffer.push(d3);

        SweepPositionData latest;
        REQUIRE(buffer.getLatest(latest));
        REQUIRE(latest.centerFreqHz == Approx(300.0f));

        // Count should be unchanged
        REQUIRE(buffer.count() == 3);

        // getLatest again should return same value
        SweepPositionData latest2;
        REQUIRE(buffer.getLatest(latest2));
        REQUIRE(latest2.centerFreqHz == Approx(300.0f));
    }

    SECTION("getLatest works after partial drain") {
        SweepPositionData d1, d2, d3;
        d1.centerFreqHz = 100.0f;
        d2.centerFreqHz = 200.0f;
        d3.centerFreqHz = 300.0f;

        buffer.push(d1);
        buffer.push(d2);
        buffer.push(d3);

        // Pop one
        SweepPositionData temp;
        buffer.pop(temp);

        // getLatest should still return the newest (300 Hz)
        SweepPositionData latest;
        REQUIRE(buffer.getLatest(latest));
        REQUIRE(latest.centerFreqHz == Approx(300.0f));
    }
}

TEST_CASE("SweepPositionBuffer: clear", "[sweep][buffer][primitives]") {
    SweepPositionBuffer buffer;

    SECTION("clear empties the buffer") {
        // Add some data
        for (int i = 0; i < 5; ++i) {
            SweepPositionData data;
            buffer.push(data);
        }

        REQUIRE_FALSE(buffer.isEmpty());
        REQUIRE(buffer.count() == 5);

        buffer.clear();

        REQUIRE(buffer.isEmpty());
        REQUIRE(buffer.count() == 0);
    }

    SECTION("can push after clear") {
        for (int i = 0; i < 5; ++i) {
            SweepPositionData data;
            buffer.push(data);
        }

        buffer.clear();

        SweepPositionData newData;
        newData.centerFreqHz = 1234.0f;
        REQUIRE(buffer.push(newData));

        SweepPositionData out;
        REQUIRE(buffer.pop(out));
        REQUIRE(out.centerFreqHz == Approx(1234.0f));
    }
}

TEST_CASE("SweepPositionBuffer: drainToLatest", "[sweep][buffer][primitives]") {
    SweepPositionBuffer buffer;

    SECTION("drainToLatest returns false on empty buffer") {
        SweepPositionData data;
        REQUIRE_FALSE(buffer.drainToLatest(data));
    }

    SECTION("drainToLatest returns last entry and empties buffer") {
        for (int i = 0; i < 5; ++i) {
            SweepPositionData data;
            data.centerFreqHz = static_cast<float>(100 * (i + 1));
            buffer.push(data);
        }

        SweepPositionData latest;
        REQUIRE(buffer.drainToLatest(latest));
        REQUIRE(latest.centerFreqHz == Approx(500.0f));  // Last pushed
        REQUIRE(buffer.isEmpty());
    }
}

TEST_CASE("SweepPositionBuffer: interpolation", "[sweep][buffer][primitives]") {
    SweepPositionBuffer buffer;

    SECTION("interpolation with single entry returns that entry") {
        SweepPositionData d1;
        d1.centerFreqHz = 1000.0f;
        d1.samplePosition = 100;
        buffer.push(d1);

        auto result = buffer.getInterpolatedPosition(150);
        REQUIRE(result.centerFreqHz == Approx(1000.0f));
    }

    SECTION("interpolation between two entries") {
        SweepPositionData d1, d2;
        d1.centerFreqHz = 1000.0f;
        d1.widthOctaves = 1.0f;
        d1.intensity = 0.5f;
        d1.samplePosition = 100;

        d2.centerFreqHz = 2000.0f;
        d2.widthOctaves = 2.0f;
        d2.intensity = 1.0f;
        d2.samplePosition = 200;

        buffer.push(d1);
        buffer.push(d2);

        // Midpoint interpolation (sample 150)
        auto result = buffer.getInterpolatedPosition(150);
        REQUIRE(result.centerFreqHz == Approx(1500.0f));  // (1000 + 2000) / 2
        REQUIRE(result.widthOctaves == Approx(1.5f));     // (1.0 + 2.0) / 2
        REQUIRE(result.intensity == Approx(0.75f));        // (0.5 + 1.0) / 2
        REQUIRE(result.samplePosition == 150);
    }

    SECTION("interpolation at exact sample position") {
        SweepPositionData d1, d2;
        d1.centerFreqHz = 1000.0f;
        d1.samplePosition = 100;

        d2.centerFreqHz = 2000.0f;
        d2.samplePosition = 200;

        buffer.push(d1);
        buffer.push(d2);

        // Exact match for first entry
        auto result1 = buffer.getInterpolatedPosition(100);
        REQUIRE(result1.centerFreqHz == Approx(1000.0f));

        // Exact match for second entry
        auto result2 = buffer.getInterpolatedPosition(200);
        REQUIRE(result2.centerFreqHz == Approx(2000.0f));
    }

    SECTION("interpolation returns default when empty") {
        auto result = buffer.getInterpolatedPosition(100);
        REQUIRE(result.centerFreqHz == Approx(1000.0f));  // Default value
    }
}

TEST_CASE("SweepPositionData: default values", "[sweep][buffer][primitives]") {
    SweepPositionData data;

    SECTION("has sensible defaults") {
        REQUIRE(data.centerFreqHz == Approx(1000.0f));
        REQUIRE(data.widthOctaves == Approx(1.5f));
        REQUIRE(data.intensity == Approx(0.5f));
        REQUIRE(data.samplePosition == 0);
        REQUIRE(data.enabled == false);
        REQUIRE(data.falloff == 1);  // Smooth by default
    }
}
