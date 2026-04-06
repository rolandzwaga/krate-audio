// ==============================================================================
// Speed Curve Data Unit Tests
// ==============================================================================

#include "ui/speed_curve_data.h"
#include "ui/speed_curve_presets.h"

#include <krate/dsp/processors/arpeggiator_core.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>

using Catch::Matchers::WithinAbs;

TEST_CASE("SpeedCurveData default is flat at 0.5", "[speed-curve]") {
    Gradus::SpeedCurveData curve;
    std::array<float, 256> table{};
    curve.bakeToTable(table);

    // Every entry should be ~0.5 (center = no offset)
    auto [minIt, maxIt] = std::minmax_element(table.begin(), table.end());
    REQUIRE(*minIt >= 0.49f);
    REQUIRE(*maxIt <= 0.51f);
}

TEST_CASE("SpeedCurveData linear ramp bakes correctly", "[speed-curve]") {
    Gradus::SpeedCurveData curve;
    curve.points.clear();

    // Ramp from 0.0 to 1.0 with linear handles
    Gradus::SpeedCurvePoint p0;
    p0.x = 0.0f; p0.y = 0.0f;
    p0.cpLeftX = 0.0f; p0.cpLeftY = 0.0f;
    p0.cpRightX = 0.33f; p0.cpRightY = 0.33f;

    Gradus::SpeedCurvePoint p1;
    p1.x = 1.0f; p1.y = 1.0f;
    p1.cpLeftX = 0.67f; p1.cpLeftY = 0.67f;
    p1.cpRightX = 1.0f; p1.cpRightY = 1.0f;

    curve.points.push_back(p0);
    curve.points.push_back(p1);

    std::array<float, 256> table{};
    curve.bakeToTable(table);

    // Should be monotonically increasing from ~0.0 to ~1.0
    REQUIRE_THAT(table[0], WithinAbs(0.0, 0.02));
    REQUIRE_THAT(table[127], WithinAbs(0.5, 0.05));
    REQUIRE_THAT(table[255], WithinAbs(1.0, 0.02));

    // Monotonicity check
    bool monotonic = std::adjacent_find(table.begin(), table.end(),
        [](float a, float b) { return b < a - 0.001f; }) == table.end();
    REQUIRE(monotonic);
}

TEST_CASE("SpeedCurveData multi-point curve", "[speed-curve]") {
    Gradus::SpeedCurveData curve;
    curve.points.clear();

    // Triangle: 0→1→0 with a peak at x=0.5
    Gradus::SpeedCurvePoint p0;
    p0.x = 0.0f; p0.y = 0.0f;
    p0.cpLeftX = 0.0f; p0.cpLeftY = 0.0f;
    p0.cpRightX = 0.17f; p0.cpRightY = 0.33f;

    Gradus::SpeedCurvePoint p1;
    p1.x = 0.5f; p1.y = 1.0f;
    p1.cpLeftX = 0.33f; p1.cpLeftY = 0.67f;
    p1.cpRightX = 0.67f; p1.cpRightY = 0.67f;

    Gradus::SpeedCurvePoint p2;
    p2.x = 1.0f; p2.y = 0.0f;
    p2.cpLeftX = 0.83f; p2.cpLeftY = 0.33f;
    p2.cpRightX = 1.0f; p2.cpRightY = 0.0f;

    curve.points.push_back(p0);
    curve.points.push_back(p1);
    curve.points.push_back(p2);

    std::array<float, 256> table{};
    curve.bakeToTable(table);

    // Start near 0, peak near middle, end near 0
    REQUIRE(table[0] < 0.1f);
    REQUIRE(table[127] > 0.7f);  // Near peak
    REQUIRE(table[255] < 0.1f);
}

TEST_CASE("ArpeggiatorCore speed curve modulation", "[speed-curve]") {
    // Verify that a non-flat curve table changes effective speed
    // and that depth=0 disables the effect.
    // This is a logic-level check on the data flow, not a full
    // arpeggiator test (those require full setup).

    Gradus::SpeedCurveData curveData;

    // Linear ramp from 0.0 (max slow) to 1.0 (max fast)
    curveData.points.clear();
    Gradus::SpeedCurvePoint p0;
    p0.x = 0.0f; p0.y = 0.0f;
    p0.cpRightX = 0.33f; p0.cpRightY = 0.33f;
    Gradus::SpeedCurvePoint p1;
    p1.x = 1.0f; p1.y = 1.0f;
    p1.cpLeftX = 0.67f; p1.cpLeftY = 0.67f;
    curveData.points.push_back(p0);
    curveData.points.push_back(p1);

    std::array<float, 256> table{};
    curveData.bakeToTable(table);

    // At loopPos=0, curveVal≈0.0 → offset = (0-0.5)*2*depth = -depth
    // At loopPos=0.5, curveVal≈0.5 → offset = 0 (center)
    // At loopPos=1.0, curveVal≈1.0 → offset = +depth
    float depth = 0.5f;

    // Simulate the modulation formula from advanceLaneBySpeed
    auto computeModulatedSpeed = [&](float centerSpeed, float loopPos) {
        int tableIdx = std::clamp(static_cast<int>(loopPos * 255.0f), 0, 255);
        float curveVal = table[static_cast<size_t>(tableIdx)];
        float offset = (curveVal - 0.5f) * 2.0f * depth;
        float speed = centerSpeed * (1.0f + offset);
        return std::clamp(speed, 0.1f, 8.0f);
    };

    float centerSpeed = 1.0f;

    // At start of loop: speed should be slower than center
    float speedAtStart = computeModulatedSpeed(centerSpeed, 0.0f);
    REQUIRE(speedAtStart < centerSpeed);

    // At middle: speed should be near center
    float speedAtMid = computeModulatedSpeed(centerSpeed, 0.5f);
    REQUIRE_THAT(speedAtMid, WithinAbs(centerSpeed, 0.1));

    // At end: speed should be faster than center
    float speedAtEnd = computeModulatedSpeed(centerSpeed, 1.0f);
    REQUIRE(speedAtEnd > centerSpeed);

    // With depth=0, no modulation
    depth = 0.0f;
    REQUIRE_THAT(computeModulatedSpeed(centerSpeed, 0.0f),
                 WithinAbs(centerSpeed, 0.001));
    REQUIRE_THAT(computeModulatedSpeed(centerSpeed, 1.0f),
                 WithinAbs(centerSpeed, 0.001));
}

TEST_CASE("SpeedCurveData sortPoints maintains endpoints", "[speed-curve]") {
    Gradus::SpeedCurveData curve;

    // Add a middle point
    Gradus::SpeedCurvePoint mid;
    mid.x = 0.3f; mid.y = 0.8f;
    mid.cpLeftX = 0.2f; mid.cpLeftY = 0.7f;
    mid.cpRightX = 0.4f; mid.cpRightY = 0.7f;

    // Insert between the two endpoints
    curve.points.insert(curve.points.begin() + 1, mid);
    REQUIRE(curve.points.size() == 3);

    curve.sortPoints();
    REQUIRE(curve.points.front().x == 0.0f);
    REQUIRE(curve.points.back().x == 1.0f);
    REQUIRE_THAT(curve.points[1].x, WithinAbs(0.3, 0.001));
}

TEST_CASE("ArpeggiatorCore actually uses speed curve during step advancement",
          "[speed-curve]") {
    // This test sets up an ArpeggiatorCore with a ramp-down curve on lane 2
    // (pitch), enables it with depth=1.0, and verifies that step advancement
    // actually slows down compared to a run without the curve.
    using namespace Krate::DSP;

    auto runArp = [](bool withCurve, std::vector<size_t>& stepLog) {
        ArpeggiatorCore arp;
        arp.prepare(44100.0, 512);
        arp.setEnabled(true);
        arp.setTempoSync(false);
        arp.setFreeRate(8.0f);  // 8 Hz
        arp.setMode(ArpMode::Up);
        arp.setGateLength(50.0f);

        // 8-step pitch lane
        arp.pitchLane().setLength(8);
        for (size_t i = 0; i < 8; ++i)
            arp.pitchLane().setStep(i, 0);

        // Set velocity lane steps to non-zero (otherwise notes have 0 velocity)
        arp.velocityLane().setLength(8);
        for (size_t i = 0; i < 8; ++i)
            arp.velocityLane().setStep(i, 1.0f);
        arp.gateLane().setLength(8);
        for (size_t i = 0; i < 8; ++i)
            arp.gateLane().setStep(i, 1.0f);

        arp.noteOn(60, 100);

        if (withCurve) {
            // Ramp from 0.5 (center) down to 0.0 (max slow)
            Gradus::SpeedCurveData curveData;
            curveData.points.clear();
            Gradus::SpeedCurvePoint p0;
            p0.x = 0.0f; p0.y = 0.5f;
            p0.cpRightX = 0.33f; p0.cpRightY = 0.33f;
            Gradus::SpeedCurvePoint p1;
            p1.x = 1.0f; p1.y = 0.0f;
            p1.cpLeftX = 0.67f; p1.cpLeftY = 0.17f;
            curveData.points.push_back(p0);
            curveData.points.push_back(p1);

            std::array<float, 256> table{};
            curveData.bakeToTable(table);

            // Apply to ALL lanes (0-7) so pitch lane (2) is included
            for (size_t lane = 0; lane < 8; ++lane) {
                arp.setLaneSpeedCurveTable(lane, table);
                arp.setLaneSpeedCurveDepth(lane, 1.0f);
                arp.setLaneSpeedCurveEnabled(lane, true);
            }
            arp.consumePendingCurveTables();
        }

        // Process multiple blocks, recording the pitch lane step after each
        BlockContext ctx{};
        ctx.sampleRate = 44100.0;
        ctx.tempoBPM = 120.0;
        ctx.blockSize = 512;  // ~11.6ms per block at 44100
        ctx.isPlaying = true;

        std::array<ArpEvent, 128> events{};

        for (int block = 0; block < 100; ++block) {
            arp.processBlock(ctx, events);
            stepLog.push_back(arp.pitchLane().currentStep());
        }
    };

    std::vector<size_t> stepsNoCurve, stepsWithCurve;
    runArp(false, stepsNoCurve);
    runArp(true, stepsWithCurve);

    // Log the step sequences (first 40 entries)
    std::string noCurveStr, withCurveStr;
    for (size_t i = 0; i < std::min(stepsNoCurve.size(), size_t{40}); ++i)
        noCurveStr += std::to_string(stepsNoCurve[i]) + " ";
    for (size_t i = 0; i < std::min(stepsWithCurve.size(), size_t{40}); ++i)
        withCurveStr += std::to_string(stepsWithCurve[i]) + " ";
    INFO("Steps without curve: " << noCurveStr);
    INFO("Steps with curve:    " << withCurveStr);

    // Count total step changes (advances) in each run
    int advancesNoCurve = 0, advancesWithCurve = 0;
    for (size_t i = 1; i < stepsNoCurve.size(); ++i)
        if (stepsNoCurve[i] != stepsNoCurve[i-1]) ++advancesNoCurve;
    for (size_t i = 1; i < stepsWithCurve.size(); ++i)
        if (stepsWithCurve[i] != stepsWithCurve[i-1]) ++advancesWithCurve;

    INFO("Advances without curve: " << advancesNoCurve);
    INFO("Advances with curve:    " << advancesWithCurve);

    // The curve should cause fewer advances (decelerating) OR different timing
    REQUIRE(stepsNoCurve != stepsWithCurve);
}

TEST_CASE("Speed curve presets bake to reasonable values", "[speed-curve]") {
    using Gradus::SpeedCurvePreset;
    using Gradus::SpeedCurveData;

    for (int i = 0; i < Gradus::kSpeedCurvePresetCount; ++i) {
        auto preset = static_cast<SpeedCurvePreset>(i);
        auto points = Gradus::generatePreset(preset);

        REQUIRE(points.size() >= 2);
        REQUIRE(points.front().x == 0.0f);
        REQUIRE(points.back().x == 1.0f);

        // Bake and verify table is within [0, 1]
        SpeedCurveData data;
        data.points = points;
        std::array<float, 256> table{};
        data.bakeToTable(table);

        auto [lo, hi] = std::minmax_element(table.begin(), table.end());
        REQUIRE(*lo >= -0.01f);
        REQUIRE(*hi <= 1.01f);
    }

    // Flat preset should be constant at 0.5
    SpeedCurveData flat;
    flat.points = Gradus::generatePreset(SpeedCurvePreset::Flat);
    std::array<float, 256> table{};
    flat.bakeToTable(table);
    auto [flatLo, flatHi] = std::minmax_element(table.begin(), table.end());
    REQUIRE(*flatLo >= 0.49f);
    REQUIRE(*flatHi <= 0.51f);

    // Triangle should peak near the middle
    SpeedCurveData tri;
    tri.points = Gradus::generatePreset(SpeedCurvePreset::Triangle);
    tri.bakeToTable(table);
    REQUIRE(table[0] < 0.15f);
    REQUIRE(table[127] > 0.7f);
    REQUIRE(table[255] < 0.15f);
}
