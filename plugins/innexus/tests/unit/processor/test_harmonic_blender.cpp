// ==============================================================================
// HarmonicBlender Tests (M6)
// ==============================================================================
// Tests for multi-source spectral blending across memory slots + live source.
//
// Feature: 120-creative-extensions
// User Story 5: Multi-Source Blending
// Requirements: FR-034 through FR-042, FR-044, FR-048, FR-052, SC-006, SC-007,
//               SC-008, SC-009, SC-011
//
// T039: Unit tests for HarmonicBlender class
// T040: Integration test for blend priority over evolution
// T041: Integration test for live source blending
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/harmonic_blender.h"
#include "dsp/evolution_engine.h"

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/sample_analysis.h"

#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/harmonic_frame_utils.h>
#include <krate/dsp/processors/harmonic_snapshot.h>
#include <krate/dsp/processors/residual_types.h>

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

// =============================================================================
// Test Infrastructure
// =============================================================================

/// Build a populated MemorySlot with distinct spectral content.
static Krate::DSP::MemorySlot makeBlendTestSlot(int slotIndex, bool occupied = true)
{
    Krate::DSP::MemorySlot slot{};
    slot.occupied = occupied;
    if (!occupied) return slot;

    auto& snap = slot.snapshot;
    snap.numPartials = 16;
    snap.f0Reference = 200.0f + static_cast<float>(slotIndex) * 100.0f;
    snap.globalAmplitude = 0.5f;
    snap.spectralCentroid = 500.0f + static_cast<float>(slotIndex) * 200.0f;
    snap.brightness = 0.3f + static_cast<float>(slotIndex) * 0.1f;

    float sumSq = 0.0f;
    for (int p = 0; p < snap.numPartials; ++p)
    {
        float n = static_cast<float>(p + 1);
        float amp = 1.0f / std::pow(n, 1.0f + static_cast<float>(slotIndex) * 0.3f);
        snap.normalizedAmps[static_cast<size_t>(p)] = amp;
        snap.relativeFreqs[static_cast<size_t>(p)] = n;
        snap.inharmonicDeviation[static_cast<size_t>(p)] = 0.0f;
        snap.phases[static_cast<size_t>(p)] = 0.0f;
        sumSq += amp * amp;
    }
    if (sumSq > 0.0f)
    {
        float invNorm = 1.0f / std::sqrt(sumSq);
        for (int p = 0; p < snap.numPartials; ++p)
            snap.normalizedAmps[static_cast<size_t>(p)] *= invNorm;
    }

    // Set residual bands
    for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
    {
        snap.residualBands[b] = 0.1f * static_cast<float>(slotIndex + 1) *
                                (1.0f / static_cast<float>(b + 1));
    }
    snap.residualEnergy = 0.05f * static_cast<float>(slotIndex + 1);

    return slot;
}

/// Build a HarmonicFrame from a MemorySlot snapshot (for direct comparison).
static void recallSlotToFrame(const Krate::DSP::MemorySlot& slot,
                              Krate::DSP::HarmonicFrame& frame,
                              Krate::DSP::ResidualFrame& residual)
{
    Krate::DSP::recallSnapshotToFrame(slot.snapshot, frame, residual);
}

// =============================================================================
// T039: Unit Tests for HarmonicBlender
// =============================================================================

TEST_CASE("HarmonicBlender: equal-weight blend of 2 sources (FR-037)", "[blender][dsp]")
{
    Innexus::HarmonicBlender blender;

    std::array<Krate::DSP::MemorySlot, 8> slots{};
    slots[0] = makeBlendTestSlot(0);
    slots[1] = makeBlendTestSlot(1);

    blender.setSlotWeight(0, 1.0f);
    blender.setSlotWeight(1, 1.0f);

    Krate::DSP::HarmonicFrame liveFrame{};
    Krate::DSP::ResidualFrame liveResidual{};
    Krate::DSP::HarmonicFrame outFrame{};
    Krate::DSP::ResidualFrame outResidual{};

    bool valid = blender.blend(slots, liveFrame, liveResidual, false, outFrame, outResidual);
    REQUIRE(valid);

    // Each source should contribute 0.5 weight (normalized)
    REQUIRE(blender.getEffectiveSlotWeight(0) == Approx(0.5f).margin(1e-6f));
    REQUIRE(blender.getEffectiveSlotWeight(1) == Approx(0.5f).margin(1e-6f));

    // Recall both slots to frames for comparison
    Krate::DSP::HarmonicFrame frameA{}, frameB{};
    Krate::DSP::ResidualFrame residA{}, residB{};
    recallSlotToFrame(slots[0], frameA, residA);
    recallSlotToFrame(slots[1], frameB, residB);

    // Verify blended amplitudes = 0.5 * A + 0.5 * B
    for (int i = 0; i < std::max(frameA.numPartials, frameB.numPartials); ++i)
    {
        float ampA = (i < frameA.numPartials) ? frameA.partials[static_cast<size_t>(i)].amplitude : 0.0f;
        float ampB = (i < frameB.numPartials) ? frameB.partials[static_cast<size_t>(i)].amplitude : 0.0f;
        float expected = 0.5f * ampA + 0.5f * ampB;
        REQUIRE(outFrame.partials[static_cast<size_t>(i)].amplitude == Approx(expected).margin(1e-5f));
    }

    // Verify blended residual bands
    for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
    {
        float expected = 0.5f * residA.bandEnergies[b] + 0.5f * residB.bandEnergies[b];
        REQUIRE(outResidual.bandEnergies[b] == Approx(expected).margin(1e-5f));
    }
}

TEST_CASE("HarmonicBlender: weight normalization with 3 sources (FR-035)", "[blender][dsp]")
{
    Innexus::HarmonicBlender blender;

    std::array<Krate::DSP::MemorySlot, 8> slots{};
    slots[0] = makeBlendTestSlot(0);
    slots[1] = makeBlendTestSlot(1);
    slots[2] = makeBlendTestSlot(2);

    blender.setSlotWeight(0, 0.2f);
    blender.setSlotWeight(1, 0.3f);
    blender.setSlotWeight(2, 0.5f);

    Krate::DSP::HarmonicFrame liveFrame{};
    Krate::DSP::ResidualFrame liveResidual{};
    Krate::DSP::HarmonicFrame outFrame{};
    Krate::DSP::ResidualFrame outResidual{};

    bool valid = blender.blend(slots, liveFrame, liveResidual, false, outFrame, outResidual);
    REQUIRE(valid);

    // Total weight = 0.2 + 0.3 + 0.5 = 1.0
    // Effective weights should equal raw weights when total = 1.0
    REQUIRE(blender.getEffectiveSlotWeight(0) == Approx(0.2f).margin(1e-6f));
    REQUIRE(blender.getEffectiveSlotWeight(1) == Approx(0.3f).margin(1e-6f));
    REQUIRE(blender.getEffectiveSlotWeight(2) == Approx(0.5f).margin(1e-6f));
}

TEST_CASE("HarmonicBlender: zero-weight source contributes nothing", "[blender][dsp]")
{
    Innexus::HarmonicBlender blender;

    std::array<Krate::DSP::MemorySlot, 8> slots{};
    slots[0] = makeBlendTestSlot(0);
    slots[1] = makeBlendTestSlot(1);

    blender.setSlotWeight(0, 1.0f);
    blender.setSlotWeight(1, 0.0f); // zero weight

    Krate::DSP::HarmonicFrame liveFrame{};
    Krate::DSP::ResidualFrame liveResidual{};
    Krate::DSP::HarmonicFrame outFrame{};
    Krate::DSP::ResidualFrame outResidual{};

    bool valid = blender.blend(slots, liveFrame, liveResidual, false, outFrame, outResidual);
    REQUIRE(valid);

    // Output should match slot 0 exactly (only source with weight)
    Krate::DSP::HarmonicFrame frameA{};
    Krate::DSP::ResidualFrame residA{};
    recallSlotToFrame(slots[0], frameA, residA);

    for (int i = 0; i < frameA.numPartials; ++i)
    {
        REQUIRE(outFrame.partials[static_cast<size_t>(i)].amplitude ==
                Approx(frameA.partials[static_cast<size_t>(i)].amplitude).margin(1e-5f));
    }
}

TEST_CASE("HarmonicBlender: all-zero weights produces silence (FR-039)", "[blender][dsp]")
{
    Innexus::HarmonicBlender blender;

    std::array<Krate::DSP::MemorySlot, 8> slots{};
    slots[0] = makeBlendTestSlot(0);
    slots[1] = makeBlendTestSlot(1);

    // All weights are zero (default)

    Krate::DSP::HarmonicFrame liveFrame{};
    Krate::DSP::ResidualFrame liveResidual{};
    Krate::DSP::HarmonicFrame outFrame{};
    Krate::DSP::ResidualFrame outResidual{};

    bool valid = blender.blend(slots, liveFrame, liveResidual, false, outFrame, outResidual);
    REQUIRE_FALSE(valid);

    // Output should be silence
    for (int i = 0; i < static_cast<int>(Krate::DSP::kMaxPartials); ++i)
    {
        REQUIRE(outFrame.partials[static_cast<size_t>(i)].amplitude == 0.0f);
    }
    for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
    {
        REQUIRE(outResidual.bandEnergies[b] == 0.0f);
    }
}

TEST_CASE("HarmonicBlender: partial count mismatch (FR-038)", "[blender][dsp]")
{
    Innexus::HarmonicBlender blender;

    std::array<Krate::DSP::MemorySlot, 8> slots{};
    // Slot 0 has 24 partials, slot 1 has 8 partials
    slots[0] = makeBlendTestSlot(0);
    slots[0].snapshot.numPartials = 24;
    for (int p = 16; p < 24; ++p)
    {
        float n = static_cast<float>(p + 1);
        slots[0].snapshot.normalizedAmps[static_cast<size_t>(p)] = 0.1f;
        slots[0].snapshot.relativeFreqs[static_cast<size_t>(p)] = n;
    }

    slots[1] = makeBlendTestSlot(1);
    slots[1].snapshot.numPartials = 8;

    blender.setSlotWeight(0, 1.0f);
    blender.setSlotWeight(1, 1.0f);

    Krate::DSP::HarmonicFrame liveFrame{};
    Krate::DSP::ResidualFrame liveResidual{};
    Krate::DSP::HarmonicFrame outFrame{};
    Krate::DSP::ResidualFrame outResidual{};

    bool valid = blender.blend(slots, liveFrame, liveResidual, false, outFrame, outResidual);
    REQUIRE(valid);

    // For partials 8-23 (beyond slot 1's count), slot 1 contributes zero (FR-038)
    // So blended amplitude for partial 20 = 0.5 * slot0_amp + 0.5 * 0.0
    Krate::DSP::HarmonicFrame frameA{};
    Krate::DSP::ResidualFrame residA{};
    recallSlotToFrame(slots[0], frameA, residA);

    for (int i = 8; i < 24; ++i)
    {
        float expected = 0.5f * frameA.partials[static_cast<size_t>(i)].amplitude;
        REQUIRE(outFrame.partials[static_cast<size_t>(i)].amplitude == Approx(expected).margin(1e-5f));
    }
}

TEST_CASE("HarmonicBlender: single-source at weight=1.0 identical to direct recall (SC-011)",
          "[blender][dsp]")
{
    Innexus::HarmonicBlender blender;

    std::array<Krate::DSP::MemorySlot, 8> slots{};
    slots[0] = makeBlendTestSlot(0);

    blender.setSlotWeight(0, 1.0f);

    Krate::DSP::HarmonicFrame liveFrame{};
    Krate::DSP::ResidualFrame liveResidual{};
    Krate::DSP::HarmonicFrame outFrame{};
    Krate::DSP::ResidualFrame outResidual{};

    bool valid = blender.blend(slots, liveFrame, liveResidual, false, outFrame, outResidual);
    REQUIRE(valid);

    // Recall slot 0 directly
    Krate::DSP::HarmonicFrame directFrame{};
    Krate::DSP::ResidualFrame directResidual{};
    recallSlotToFrame(slots[0], directFrame, directResidual);

    // Verify all partial amplitudes match exactly
    REQUIRE(outFrame.numPartials == directFrame.numPartials);
    for (int i = 0; i < directFrame.numPartials; ++i)
    {
        REQUIRE(outFrame.partials[static_cast<size_t>(i)].amplitude ==
                Approx(directFrame.partials[static_cast<size_t>(i)].amplitude).margin(1e-6f));
        REQUIRE(outFrame.partials[static_cast<size_t>(i)].relativeFrequency ==
                Approx(directFrame.partials[static_cast<size_t>(i)].relativeFrequency).margin(1e-6f));
    }

    // Verify residual bands match
    for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
    {
        REQUIRE(outResidual.bandEnergies[b] ==
                Approx(directResidual.bandEnergies[b]).margin(1e-6f));
    }
    REQUIRE(outResidual.totalEnergy == Approx(directResidual.totalEnergy).margin(1e-6f));
}

TEST_CASE("HarmonicBlender: empty slot contributes nothing regardless of weight", "[blender][dsp]")
{
    Innexus::HarmonicBlender blender;

    std::array<Krate::DSP::MemorySlot, 8> slots{};
    slots[0] = makeBlendTestSlot(0);
    slots[1].occupied = false; // empty slot

    blender.setSlotWeight(0, 1.0f);
    blender.setSlotWeight(1, 1.0f); // weight set but slot empty

    Krate::DSP::HarmonicFrame liveFrame{};
    Krate::DSP::ResidualFrame liveResidual{};
    Krate::DSP::HarmonicFrame outFrame{};
    Krate::DSP::ResidualFrame outResidual{};

    bool valid = blender.blend(slots, liveFrame, liveResidual, false, outFrame, outResidual);
    REQUIRE(valid);

    // Only slot 0 should contribute -- effective weight = 1.0
    REQUIRE(blender.getEffectiveSlotWeight(0) == Approx(1.0f).margin(1e-6f));
    REQUIRE(blender.getEffectiveSlotWeight(1) == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("HarmonicBlender: blended centroid within +/-10% of mean (SC-006)", "[blender][dsp]")
{
    Innexus::HarmonicBlender blender;

    std::array<Krate::DSP::MemorySlot, 8> slots{};
    slots[0] = makeBlendTestSlot(0); // centroid ~500 Hz
    slots[2] = makeBlendTestSlot(2); // centroid ~900 Hz

    blender.setSlotWeight(0, 1.0f);
    blender.setSlotWeight(2, 1.0f);

    Krate::DSP::HarmonicFrame liveFrame{};
    Krate::DSP::ResidualFrame liveResidual{};
    Krate::DSP::HarmonicFrame outFrame{};
    Krate::DSP::ResidualFrame outResidual{};

    bool valid = blender.blend(slots, liveFrame, liveResidual, false, outFrame, outResidual);
    REQUIRE(valid);

    // Compute amplitude-weighted centroid of the blended output
    // The spectralCentroid metadata is blended, so check that
    float centroidA = slots[0].snapshot.spectralCentroid;
    float centroidB = slots[2].snapshot.spectralCentroid;
    float expectedMean = (centroidA + centroidB) / 2.0f;

    // SC-006: blended centroid within +/-10% of arithmetic mean
    REQUIRE(outFrame.spectralCentroid >= expectedMean * 0.9f);
    REQUIRE(outFrame.spectralCentroid <= expectedMean * 1.1f);
}

// =============================================================================
// T040: Blend Priority Over Evolution (FR-052)
// =============================================================================

TEST_CASE("HarmonicBlender: blend overrides evolution when both enabled (FR-052)",
          "[blender][integration]")
{
    // This test verifies at the unit level that when blendEnabled is true,
    // the evolution engine output is NOT used -- the blender output is used instead.
    // The actual integration is in the processor, but we verify the blender
    // produces output independently of evolution.
    Innexus::HarmonicBlender blender;
    Innexus::EvolutionEngine engine;

    engine.prepare(44100.0);

    std::array<Krate::DSP::MemorySlot, 8> slots{};
    slots[0] = makeBlendTestSlot(0);
    slots[1] = makeBlendTestSlot(1);
    slots[2] = makeBlendTestSlot(2);

    engine.updateWaypoints(slots);
    engine.setSpeed(1.0f);
    engine.setDepth(1.0f);
    engine.setMode(Innexus::EvolutionMode::Cycle);

    // Advance evolution to a non-trivial position
    for (int i = 0; i < 22050; ++i)
        engine.advance();

    // Blender should produce its own output independent of evolution
    blender.setSlotWeight(0, 1.0f);
    blender.setSlotWeight(2, 1.0f);

    Krate::DSP::HarmonicFrame liveFrame{};
    Krate::DSP::ResidualFrame liveResidual{};
    Krate::DSP::HarmonicFrame blendFrame{};
    Krate::DSP::ResidualFrame blendResidual{};

    bool valid = blender.blend(slots, liveFrame, liveResidual, false, blendFrame, blendResidual);
    REQUIRE(valid);

    // Evolution output
    Krate::DSP::HarmonicFrame evoFrame{};
    Krate::DSP::ResidualFrame evoResidual{};
    bool evoValid = engine.getInterpolatedFrame(slots, evoFrame, evoResidual);
    REQUIRE(evoValid);

    // The blender output should differ from evolution output (different weighting)
    // This verifies they are independent paths
    bool anyDifference = false;
    for (int i = 0; i < 16; ++i)
    {
        if (std::abs(blendFrame.partials[static_cast<size_t>(i)].amplitude -
                     evoFrame.partials[static_cast<size_t>(i)].amplitude) > 1e-5f)
        {
            anyDifference = true;
            break;
        }
    }
    REQUIRE(anyDifference);
}

// =============================================================================
// T041: Live Source Blending
// =============================================================================

TEST_CASE("HarmonicBlender: live source blending (FR-036, FR-037)", "[blender][dsp]")
{
    Innexus::HarmonicBlender blender;

    std::array<Krate::DSP::MemorySlot, 8> slots{};
    slots[0] = makeBlendTestSlot(0);

    // Build a live frame with distinct content
    Krate::DSP::HarmonicFrame liveFrame{};
    liveFrame.numPartials = 16;
    liveFrame.f0 = 440.0f;
    liveFrame.f0Confidence = 1.0f;
    liveFrame.globalAmplitude = 0.8f;
    liveFrame.spectralCentroid = 1200.0f;
    liveFrame.brightness = 0.7f;
    for (int i = 0; i < 16; ++i)
    {
        auto& p = liveFrame.partials[static_cast<size_t>(i)];
        float n = static_cast<float>(i + 1);
        p.harmonicIndex = i + 1;
        p.relativeFrequency = n;
        p.amplitude = 0.5f / n; // Different from slot
        p.phase = 0.0f;
        p.inharmonicDeviation = 0.0f;
        p.stability = 1.0f;
        p.age = 1;
    }

    Krate::DSP::ResidualFrame liveResidual{};
    for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
        liveResidual.bandEnergies[b] = 0.2f;
    liveResidual.totalEnergy = 0.15f;

    blender.setSlotWeight(0, 0.5f);
    blender.setLiveWeight(0.5f);

    Krate::DSP::HarmonicFrame outFrame{};
    Krate::DSP::ResidualFrame outResidual{};

    bool valid = blender.blend(slots, liveFrame, liveResidual, true, outFrame, outResidual);
    REQUIRE(valid);

    // Recall slot 0 for comparison
    Krate::DSP::HarmonicFrame slotFrame{};
    Krate::DSP::ResidualFrame slotResidual{};
    recallSlotToFrame(slots[0], slotFrame, slotResidual);

    // Each should have effective weight = 0.5
    REQUIRE(blender.getEffectiveSlotWeight(0) == Approx(0.5f).margin(1e-6f));
    REQUIRE(blender.getEffectiveLiveWeight() == Approx(0.5f).margin(1e-6f));

    // Verify blended amplitudes = 0.5 * slot + 0.5 * live
    for (int i = 0; i < 16; ++i)
    {
        float ampSlot = slotFrame.partials[static_cast<size_t>(i)].amplitude;
        float ampLive = liveFrame.partials[static_cast<size_t>(i)].amplitude;
        float expected = 0.5f * ampSlot + 0.5f * ampLive;
        REQUIRE(outFrame.partials[static_cast<size_t>(i)].amplitude ==
                Approx(expected).margin(1e-5f));
    }

    // Verify blended residual
    for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
    {
        float expected = 0.5f * slotResidual.bandEnergies[b] + 0.5f * liveResidual.bandEnergies[b];
        REQUIRE(outResidual.bandEnergies[b] == Approx(expected).margin(1e-5f));
    }
}

TEST_CASE("HarmonicBlender: live source ignored when hasLiveSource is false", "[blender][dsp]")
{
    Innexus::HarmonicBlender blender;

    std::array<Krate::DSP::MemorySlot, 8> slots{};
    slots[0] = makeBlendTestSlot(0);

    Krate::DSP::HarmonicFrame liveFrame{};
    liveFrame.numPartials = 16;
    for (int i = 0; i < 16; ++i)
        liveFrame.partials[static_cast<size_t>(i)].amplitude = 1.0f;

    Krate::DSP::ResidualFrame liveResidual{};
    Krate::DSP::HarmonicFrame outFrame{};
    Krate::DSP::ResidualFrame outResidual{};

    blender.setSlotWeight(0, 1.0f);
    blender.setLiveWeight(1.0f); // Set but hasLiveSource=false

    bool valid = blender.blend(slots, liveFrame, liveResidual, false, outFrame, outResidual);
    REQUIRE(valid);

    // Live source weight should be zero in effective weights
    REQUIRE(blender.getEffectiveLiveWeight() == Approx(0.0f).margin(1e-6f));
    REQUIRE(blender.getEffectiveSlotWeight(0) == Approx(1.0f).margin(1e-6f));
}
