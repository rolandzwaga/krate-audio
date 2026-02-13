// ==============================================================================
// Tests: VoiceModRouter
// ==============================================================================
// Unit tests for the per-voice modulation routing system.
//
// Feature: 041-ruinae-voice-architecture (User Story 6)
// Test-First: Constitution Principle XII
// ==============================================================================

#include <krate/dsp/systems/voice_mod_router.h>
#include <krate/dsp/systems/voice_mod_types.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <chrono>
#include <cmath>
#include <limits>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Helpers
// =============================================================================

namespace {

constexpr float kEnv1Value = 0.0f;
constexpr float kEnv2Value = 0.8f;
constexpr float kEnv3Value = 0.5f;
constexpr float kLfoValue = -0.3f;
constexpr float kGateValue = 1.0f;
constexpr float kVelocityValue = 0.75f;
constexpr float kKeyTrackValue = 0.2f;  // (midiNote - 60) / 60

} // anonymous namespace

// =============================================================================
// Empty Router Produces Zero Offsets
// =============================================================================

TEST_CASE("VoiceModRouter: empty router produces zero offsets",
          "[voice_mod_router]") {
    VoiceModRouter router;
    router.computeOffsets(0.5f, 0.8f, 0.3f, -0.5f, 1.0f, 0.75f, 0.2f, 0.0f);

    REQUIRE(router.getOffset(VoiceModDest::FilterCutoff) == Approx(0.0f));
    REQUIRE(router.getOffset(VoiceModDest::FilterResonance) == Approx(0.0f));
    REQUIRE(router.getOffset(VoiceModDest::MorphPosition) == Approx(0.0f));
    REQUIRE(router.getOffset(VoiceModDest::DistortionDrive) == Approx(0.0f));
    REQUIRE(router.getOffset(VoiceModDest::TranceGateDepth) == Approx(0.0f));
    REQUIRE(router.getOffset(VoiceModDest::OscAPitch) == Approx(0.0f));
    REQUIRE(router.getOffset(VoiceModDest::OscBPitch) == Approx(0.0f));
}

// =============================================================================
// Single Route: Env2 -> FilterCutoff
// =============================================================================

TEST_CASE("VoiceModRouter: single route Env2 -> FilterCutoff",
          "[voice_mod_router]") {
    VoiceModRouter router;

    VoiceModRoute route;
    route.source = VoiceModSource::Env2;
    route.destination = VoiceModDest::FilterCutoff;
    route.amount = 1.0f;  // +1.0 means full range (48 semitones scaled by caller)

    router.setRoute(0, route);

    // Env2 value = 0.8, amount = 1.0 -> offset = 0.8 * 1.0 = 0.8
    router.computeOffsets(kEnv1Value, kEnv2Value, kEnv3Value,
                          kLfoValue, kGateValue, kVelocityValue, kKeyTrackValue, 0.0f);

    REQUIRE(router.getOffset(VoiceModDest::FilterCutoff) == Approx(0.8f));
    // Other destinations should be zero
    REQUIRE(router.getOffset(VoiceModDest::MorphPosition) == Approx(0.0f));
}

// =============================================================================
// Two Routes to Same Destination are Summed (FR-027, AS-6.4)
// =============================================================================

TEST_CASE("VoiceModRouter: two routes to same destination are summed",
          "[voice_mod_router][fr027]") {
    VoiceModRouter router;

    // Route 0: Env2 -> FilterCutoff with amount +0.5
    VoiceModRoute route0;
    route0.source = VoiceModSource::Env2;
    route0.destination = VoiceModDest::FilterCutoff;
    route0.amount = 0.5f;
    router.setRoute(0, route0);

    // Route 1: VoiceLFO -> FilterCutoff with amount -0.25
    VoiceModRoute route1;
    route1.source = VoiceModSource::VoiceLFO;
    route1.destination = VoiceModDest::FilterCutoff;
    route1.amount = -0.25f;
    router.setRoute(1, route1);

    // Env2 = 0.8, LFO = -0.3
    // Route 0 contribution: 0.8 * 0.5 = 0.4
    // Route 1 contribution: -0.3 * -0.25 = 0.075
    // Total: 0.475
    router.computeOffsets(kEnv1Value, kEnv2Value, kEnv3Value,
                          kLfoValue, kGateValue, kVelocityValue, kKeyTrackValue, 0.0f);

    REQUIRE(router.getOffset(VoiceModDest::FilterCutoff) == Approx(0.475f));
}

// =============================================================================
// Amount Clamped to [-1.0, +1.0]
// =============================================================================

TEST_CASE("VoiceModRouter: amount is clamped to [-1.0, +1.0]",
          "[voice_mod_router]") {
    VoiceModRouter router;

    VoiceModRoute route;
    route.source = VoiceModSource::Env1;
    route.destination = VoiceModDest::OscAPitch;
    route.amount = 5.0f;  // Exceeds max, should be clamped to 1.0
    router.setRoute(0, route);

    // Env1 = 0.5
    router.computeOffsets(0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    // Clamped amount = 1.0, so offset = 0.5 * 1.0 = 0.5
    REQUIRE(router.getOffset(VoiceModDest::OscAPitch) == Approx(0.5f));
}

TEST_CASE("VoiceModRouter: negative amount exceeding -1.0 is clamped",
          "[voice_mod_router]") {
    VoiceModRouter router;

    VoiceModRoute route;
    route.source = VoiceModSource::Env1;
    route.destination = VoiceModDest::OscAPitch;
    route.amount = -3.0f;  // Exceeds min, should be clamped to -1.0
    router.setRoute(0, route);

    router.computeOffsets(0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    // Clamped amount = -1.0, so offset = 0.5 * -1.0 = -0.5
    REQUIRE(router.getOffset(VoiceModDest::OscAPitch) == Approx(-0.5f));
}

// =============================================================================
// Velocity Source is Constant Per Note
// =============================================================================

TEST_CASE("VoiceModRouter: velocity source provides constant value per note",
          "[voice_mod_router]") {
    VoiceModRouter router;

    VoiceModRoute route;
    route.source = VoiceModSource::Velocity;
    route.destination = VoiceModDest::FilterCutoff;
    route.amount = 1.0f;
    router.setRoute(0, route);

    // First call with velocity = 0.75
    router.computeOffsets(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.75f, 0.0f, 0.0f);
    REQUIRE(router.getOffset(VoiceModDest::FilterCutoff) == Approx(0.75f));

    // Second call with same velocity -- should produce same result
    router.computeOffsets(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.75f, 0.0f, 0.0f);
    REQUIRE(router.getOffset(VoiceModDest::FilterCutoff) == Approx(0.75f));
}

// =============================================================================
// All 16 Routes Functional
// =============================================================================

TEST_CASE("VoiceModRouter: 16 routes all functional",
          "[voice_mod_router]") {
    VoiceModRouter router;

    // Fill all 16 routes targeting different destinations
    for (int i = 0; i < VoiceModRouter::kMaxRoutes; ++i) {
        VoiceModRoute route;
        route.source = VoiceModSource::Env1;
        route.destination = static_cast<VoiceModDest>(i % static_cast<int>(VoiceModDest::NumDestinations));
        route.amount = 0.1f;
        router.setRoute(i, route);
    }

    REQUIRE(router.getRouteCount() == 16);

    // With 16 routes distributed across 9 destinations, some will have multiple
    // routes. Verify all routes contribute.
    router.computeOffsets(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    // FilterCutoff gets routes at indices 0, 9 (i % 9 == 0)
    // Each contributes 1.0 * 0.1 = 0.1, so total = 0.2
    REQUIRE(router.getOffset(VoiceModDest::FilterCutoff) == Approx(0.2f).margin(0.001f));
}

// =============================================================================
// Clear Route Zeroes Its Contribution
// =============================================================================

TEST_CASE("VoiceModRouter: clear route zeroes its contribution",
          "[voice_mod_router]") {
    VoiceModRouter router;

    VoiceModRoute route;
    route.source = VoiceModSource::Env2;
    route.destination = VoiceModDest::FilterCutoff;
    route.amount = 1.0f;
    router.setRoute(0, route);

    // Verify route contributes
    router.computeOffsets(0.0f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    REQUIRE(router.getOffset(VoiceModDest::FilterCutoff) == Approx(0.8f));

    // Clear the route
    router.clearRoute(0);

    // Verify contribution is zero
    router.computeOffsets(0.0f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    REQUIRE(router.getOffset(VoiceModDest::FilterCutoff) == Approx(0.0f));
}

// =============================================================================
// Clear All Routes
// =============================================================================

TEST_CASE("VoiceModRouter: clearAllRoutes resets everything",
          "[voice_mod_router]") {
    VoiceModRouter router;

    // Add several routes
    for (int i = 0; i < 5; ++i) {
        VoiceModRoute route;
        route.source = VoiceModSource::Env1;
        route.destination = VoiceModDest::FilterCutoff;
        route.amount = 0.2f;
        router.setRoute(i, route);
    }

    REQUIRE(router.getRouteCount() == 5);

    router.clearAllRoutes();

    REQUIRE(router.getRouteCount() == 0);

    router.computeOffsets(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    REQUIRE(router.getOffset(VoiceModDest::FilterCutoff) == Approx(0.0f));
}

// =============================================================================
// Each Source Type Maps Correctly
// =============================================================================

TEST_CASE("VoiceModRouter: each source type maps to correct input value",
          "[voice_mod_router]") {
    VoiceModRouter router;

    // Route each source to a different destination with amount = 1.0
    const float env1 = 0.1f;
    const float env2 = 0.2f;
    const float env3 = 0.3f;
    const float lfo = -0.4f;
    const float gate = 0.5f;
    const float velocity = 0.6f;
    const float keyTrack = 0.7f;

    // Env1 -> FilterCutoff
    router.setRoute(0, {VoiceModSource::Env1, VoiceModDest::FilterCutoff, 1.0f});
    // Env2 -> FilterResonance
    router.setRoute(1, {VoiceModSource::Env2, VoiceModDest::FilterResonance, 1.0f});
    // Env3 -> MorphPosition
    router.setRoute(2, {VoiceModSource::Env3, VoiceModDest::MorphPosition, 1.0f});
    // VoiceLFO -> DistortionDrive
    router.setRoute(3, {VoiceModSource::VoiceLFO, VoiceModDest::DistortionDrive, 1.0f});
    // GateOutput -> TranceGateDepth
    router.setRoute(4, {VoiceModSource::GateOutput, VoiceModDest::TranceGateDepth, 1.0f});
    // Velocity -> OscAPitch
    router.setRoute(5, {VoiceModSource::Velocity, VoiceModDest::OscAPitch, 1.0f});
    // KeyTrack -> OscBPitch
    router.setRoute(6, {VoiceModSource::KeyTrack, VoiceModDest::OscBPitch, 1.0f});

    router.computeOffsets(env1, env2, env3, lfo, gate, velocity, keyTrack, 0.0f);

    REQUIRE(router.getOffset(VoiceModDest::FilterCutoff) == Approx(env1));
    REQUIRE(router.getOffset(VoiceModDest::FilterResonance) == Approx(env2));
    REQUIRE(router.getOffset(VoiceModDest::MorphPosition) == Approx(env3));
    REQUIRE(router.getOffset(VoiceModDest::DistortionDrive) == Approx(lfo));
    REQUIRE(router.getOffset(VoiceModDest::TranceGateDepth) == Approx(gate));
    REQUIRE(router.getOffset(VoiceModDest::OscAPitch) == Approx(velocity));
    REQUIRE(router.getOffset(VoiceModDest::OscBPitch) == Approx(keyTrack));
}

// =============================================================================
// Out-of-Range Route Index is Ignored
// =============================================================================

TEST_CASE("VoiceModRouter: out-of-range route index is ignored",
          "[voice_mod_router]") {
    VoiceModRouter router;

    VoiceModRoute route;
    route.source = VoiceModSource::Env1;
    route.destination = VoiceModDest::FilterCutoff;
    route.amount = 1.0f;

    // These should be silently ignored (no crash)
    router.setRoute(-1, route);
    router.setRoute(16, route);
    router.setRoute(100, route);
    router.clearRoute(-1);
    router.clearRoute(16);

    REQUIRE(router.getRouteCount() == 0);
}

// =============================================================================
// getOffset with Invalid Destination Returns Zero
// =============================================================================

TEST_CASE("VoiceModRouter: getOffset with out-of-range destination returns zero",
          "[voice_mod_router]") {
    VoiceModRouter router;
    router.computeOffsets(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f);

    // NumDestinations is the sentinel -- should return 0
    REQUIRE(router.getOffset(VoiceModDest::NumDestinations) == Approx(0.0f));
    // Cast a high value
    REQUIRE(router.getOffset(static_cast<VoiceModDest>(255)) == Approx(0.0f));
}

// =============================================================================
// Bipolar Source (LFO) with Bipolar Amount
// =============================================================================

TEST_CASE("VoiceModRouter: bipolar source with negative amount",
          "[voice_mod_router]") {
    VoiceModRouter router;

    VoiceModRoute route;
    route.source = VoiceModSource::VoiceLFO;
    route.destination = VoiceModDest::OscAPitch;
    route.amount = -0.5f;
    router.setRoute(0, route);

    // LFO = -0.3, amount = -0.5 -> offset = -0.3 * -0.5 = 0.15
    router.computeOffsets(0.0f, 0.0f, 0.0f, -0.3f, 0.0f, 0.0f, 0.0f, 0.0f);
    REQUIRE(router.getOffset(VoiceModDest::OscAPitch) == Approx(0.15f));
}

// =============================================================================
// 042-ext-modulation-system: User Story 1 Tests
// =============================================================================

// T006: Aftertouch single route
TEST_CASE("VoiceModRouter: Aftertouch single route produces expected offset",
          "[voice_mod_router][ext_modulation]") {
    VoiceModRouter router;

    // Route: Aftertouch -> FilterCutoff, amount = +1.0
    router.setRoute(0, {VoiceModSource::Aftertouch, VoiceModDest::FilterCutoff, 1.0f});

    // Aftertouch = 0.6, amount = 1.0 -> offset = 0.6
    router.computeOffsets(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.6f);

    REQUIRE(router.getOffset(VoiceModDest::FilterCutoff) == Approx(0.6f));
}

// T007: Aftertouch + Env2 multi-route summation to FilterCutoff
TEST_CASE("VoiceModRouter: Aftertouch + Env2 multi-route summation",
          "[voice_mod_router][ext_modulation]") {
    VoiceModRouter router;

    // Route 0: Aftertouch -> FilterCutoff, amount = +0.5
    router.setRoute(0, {VoiceModSource::Aftertouch, VoiceModDest::FilterCutoff, 0.5f});
    // Route 1: Env2 -> FilterCutoff, amount = +0.5
    router.setRoute(1, {VoiceModSource::Env2, VoiceModDest::FilterCutoff, 0.5f});

    // Aftertouch = 0.6 -> contribution = 0.6 * 0.5 = 0.3
    // Env2 = 0.8 -> contribution = 0.8 * 0.5 = 0.4
    // Total = 0.7
    router.computeOffsets(0.0f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.6f);

    REQUIRE(router.getOffset(VoiceModDest::FilterCutoff) == Approx(0.7f));
}

// T008: Zero aftertouch produces zero contribution
TEST_CASE("VoiceModRouter: zero aftertouch produces zero contribution",
          "[voice_mod_router][ext_modulation]") {
    VoiceModRouter router;

    router.setRoute(0, {VoiceModSource::Aftertouch, VoiceModDest::MorphPosition, 1.0f});

    // Aftertouch = 0.0 -> offset = 0.0 * 1.0 = 0.0
    router.computeOffsets(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    REQUIRE(router.getOffset(VoiceModDest::MorphPosition) == Approx(0.0f));
}

// T009: OscALevel route (Env3 -> OscALevel, amount = +1.0)
TEST_CASE("VoiceModRouter: OscALevel route from Env3",
          "[voice_mod_router][ext_modulation]") {
    VoiceModRouter router;

    router.setRoute(0, {VoiceModSource::Env3, VoiceModDest::OscALevel, 1.0f});

    // Env3 = 0.5 -> offset = 0.5 * 1.0 = 0.5
    router.computeOffsets(0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    REQUIRE(router.getOffset(VoiceModDest::OscALevel) == Approx(0.5f));
}

// T010: OscBLevel route (LFO -> OscBLevel, negative amount)
TEST_CASE("VoiceModRouter: OscBLevel route with negative amount",
          "[voice_mod_router][ext_modulation]") {
    VoiceModRouter router;

    router.setRoute(0, {VoiceModSource::VoiceLFO, VoiceModDest::OscBLevel, -0.5f});

    // LFO = 0.8 -> offset = 0.8 * -0.5 = -0.4
    router.computeOffsets(0.0f, 0.0f, 0.0f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f);

    REQUIRE(router.getOffset(VoiceModDest::OscBLevel) == Approx(-0.4f));
}

// T011: NaN source value sanitized to zero (FR-024)
TEST_CASE("VoiceModRouter: NaN source value is sanitized to zero offset",
          "[voice_mod_router][ext_modulation][nan_safety]") {
    VoiceModRouter router;

    router.setRoute(0, {VoiceModSource::Env1, VoiceModDest::FilterCutoff, 1.0f});

    // Pass NaN as env1 source -- after sanitization offset should be 0.0
    const float nan = std::numeric_limits<float>::quiet_NaN();
    router.computeOffsets(nan, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    // NaN * 1.0 = NaN -> sanitized to 0.0
    REQUIRE(router.getOffset(VoiceModDest::FilterCutoff) == Approx(0.0f));
}

// T012: Denormal source value flushed to zero (FR-024)
TEST_CASE("VoiceModRouter: denormal source value flushed to zero offset",
          "[voice_mod_router][ext_modulation][nan_safety]") {
    VoiceModRouter router;

    router.setRoute(0, {VoiceModSource::Env1, VoiceModDest::FilterCutoff, 1.0f});

    // Pass denormal value as env1 source (1e-40f is denormal for float)
    router.computeOffsets(1e-40f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    // 1e-40f * 1.0f = 1e-40f -> flushed to exactly 0.0f
    REQUIRE(router.getOffset(VoiceModDest::FilterCutoff) == 0.0f);
}

// T013: No routes configured produces all-zero offsets (including new destinations)
TEST_CASE("VoiceModRouter: no routes produces all-zero offsets including OscLevel",
          "[voice_mod_router][ext_modulation]") {
    VoiceModRouter router;

    router.computeOffsets(0.5f, 0.8f, 0.3f, -0.5f, 1.0f, 0.75f, 0.2f, 0.6f);

    // All 9 destinations should be zero
    REQUIRE(router.getOffset(VoiceModDest::FilterCutoff) == Approx(0.0f));
    REQUIRE(router.getOffset(VoiceModDest::FilterResonance) == Approx(0.0f));
    REQUIRE(router.getOffset(VoiceModDest::MorphPosition) == Approx(0.0f));
    REQUIRE(router.getOffset(VoiceModDest::DistortionDrive) == Approx(0.0f));
    REQUIRE(router.getOffset(VoiceModDest::TranceGateDepth) == Approx(0.0f));
    REQUIRE(router.getOffset(VoiceModDest::OscAPitch) == Approx(0.0f));
    REQUIRE(router.getOffset(VoiceModDest::OscBPitch) == Approx(0.0f));
    REQUIRE(router.getOffset(VoiceModDest::OscALevel) == Approx(0.0f));
    REQUIRE(router.getOffset(VoiceModDest::OscBLevel) == Approx(0.0f));
}

// T011 supplement: Inf source value sanitized to zero (FR-024)
TEST_CASE("VoiceModRouter: Inf source value is sanitized to zero offset",
          "[voice_mod_router][ext_modulation][nan_safety]") {
    VoiceModRouter router;

    router.setRoute(0, {VoiceModSource::Env1, VoiceModDest::FilterCutoff, 1.0f});

    // Pass Inf as env1 source
    const float inf = std::numeric_limits<float>::infinity();
    router.computeOffsets(inf, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    // Inf * 1.0 = Inf -> sanitized to 0.0
    REQUIRE(router.getOffset(VoiceModDest::FilterCutoff) == Approx(0.0f));
}

// =============================================================================
// 042-ext-modulation-system: US7 - Performance Benchmark (SC-001)
// =============================================================================

TEST_CASE("VoiceModRouter performance: 16 routes, 8 voices, 512-sample blocks",
          "[voice_mod_router][ext_modulation][performance][SC-001]") {
    // Simulate 8 voices each with 16 routes
    constexpr size_t numVoices = 8;
    constexpr size_t blockSize = 512;
    constexpr size_t totalBlocks = (44100 * 10) / blockSize; // 10 seconds

    VoiceModRouter routers[numVoices];

    // Configure 16 routes per voice (max capacity)
    for (size_t v = 0; v < numVoices; ++v) {
        for (size_t r = 0; r < 16; ++r) {
            auto src = static_cast<VoiceModSource>(r % static_cast<size_t>(VoiceModSource::NumSources));
            auto dst = static_cast<VoiceModDest>(r % static_cast<size_t>(VoiceModDest::NumDestinations));
            float amount = (r % 2 == 0) ? 0.5f : -0.3f;
            routers[v].setRoute(static_cast<int>(r), {src, dst, amount});
        }
    }

    const auto start = std::chrono::high_resolution_clock::now();

    for (size_t block = 0; block < totalBlocks; ++block) {
        // Each block: compute offsets once per voice (block-rate, not per-sample)
        for (size_t v = 0; v < numVoices; ++v) {
            routers[v].computeOffsets(
                0.8f,  // env1
                0.5f,  // env2
                0.3f,  // env3
                std::sin(static_cast<float>(block) * 0.01f),  // lfo (varying)
                1.0f,  // gate
                0.7f,  // velocity
                0.5f,  // keyTrack
                0.4f   // aftertouch
            );
        }
    }

    const auto end = std::chrono::high_resolution_clock::now();
    const double durationMs = std::chrono::duration<double, std::milli>(end - start).count();
    const double cpuPercent = (durationMs / 10000.0) * 100.0;

    INFO("Per-voice modulation processing time: " << durationMs << " ms for 10s of audio");
    INFO("CPU usage: " << cpuPercent << "%");

    // SC-001: per-voice modulation < 0.5% CPU
    REQUIRE(cpuPercent < 0.5);
}
