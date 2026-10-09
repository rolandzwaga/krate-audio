// ==============================================================================
// Vorago Phase 13b - ecosystem levers, the [long] natural-colony set
// ==============================================================================
// Owning spec: specs/vorago-phase13b-ecosystem-audibility (plan S5.3, SC-010 (b)).
// Skeleton created by tasks.md T004; the case is written in T024 against the
// tuned levers.
//
// PUBLIC API ONLY. This TU does NOT define detail::VoragoEcosystemLeverProbe.
// The ONE definition of that friend in dsp_systems_tests lives in
// vorago_ecosystem_lever_test.cpp; defining it here as well would be an ODR
// violation.
//
// Planned case (plan S5.3): VoragoVoice_EcosystemLeverClickFreeNatural, tags
// [systems][vorago][long]. 48 kHz, one voice at ecosystem depth 1, fast attack,
// a 10-minute render. ClickDetector (the plan S5.2 pinned ClickDetectorConfig)
// on L and R; REQUIRE zero detections within +/-64 samples of any ecosystem
// control-step boundary (Clarifications Q7). Detections elsewhere and the
// depth-0 twin's count are printed, record-only. The render takes over 15 s and
// its failure mode is not toolchain-specific, hence [long].
//
// Finiteness is checked by detail::isFinite (bit-pattern, db_utils.h) only -
// never std::isnan. Voices are allocated on the heap (std::make_unique).
// ==============================================================================

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/systems/vorago_engine.h>  // VoragoEngine::kVoiceSaltBase (public)
#include <krate/dsp/systems/vorago_voice.h>

#include "artifact_detection.h"

// The shared Phase 10 fixtures: applyFastAttack (FR-014a), reused, not re-invented.
#include <vorago_fixtures.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <vector>

namespace {

using Krate::DSP::VoragoEngine;
using Krate::DSP::VoragoVoice;
using Krate::DSP::VoragoVoiceConfig;

} // namespace

// -----------------------------------------------------------------------------
// VoragoVoice_EcosystemLeverClickFreeNatural (SC-010(b); plan S5.3, tasks T024)
// -----------------------------------------------------------------------------
// One voice, 48 kHz, the natural colony at ecosystem depth 1, fast attack
// (FR-014a applyFastAttack), a 10-minute render served in 64-sample calls via
// processStereoBlock. The start sample of every chunk in which
// ecosystem().getControlStepCount() incremented is a step boundary. The pinned
// plan S5.2 ClickDetector runs on L and R over the whole render.
// GATE (Clarifications Q7): zero detections within +/-64 samples of any
// boundary. Detections elsewhere are printed with their times, and a depth-0
// twin's counts (same seed, same stimulus) are printed beside them - record
// only, never gating. The two arms reuse one pair of buffers, so only one
// 10-minute stereo render is resident at a time.
TEST_CASE("VoragoVoice_EcosystemLeverClickFreeNatural", "[systems][vorago][long]") {
    using Krate::DSP::TestUtils::ClickDetection;
    using Krate::DSP::TestUtils::ClickDetector;
    using Krate::DSP::TestUtils::ClickDetectorConfig;
    using Krate::DSP::TestUtils::Vorago::applyFastAttack;

    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kChunk = VoragoVoice::kControlChunkSamples;  // 64
    constexpr std::size_t kTotalSamples = 10u * 60u * 48000u;          // 10 min
    static_assert(kTotalSamples % kChunk == 0u, "whole control chunks only");
    constexpr std::size_t kBoundaryRadius = 64;  // +/-1 chunk (Q7)
    constexpr std::size_t kMaxPrintedDetections = 200;
    constexpr float kNoteHz = 65.406f;
    constexpr float kVelocity = 100.0f / 127.0f;
    const std::uint32_t seed =
        Krate::DSP::deriveStreamSeed(1u, VoragoEngine::kVoiceSaltBase + 0u);

    std::vector<float> l(kTotalSamples, 0.0f);
    std::vector<float> r(kTotalSamples, 0.0f);

    // Renders one arm into l/r. When `boundaries` is non-null, the start sample
    // of every chunk whose render advanced the ecosystem step count is appended.
    auto renderArm = [&](float depth, std::vector<std::size_t>* boundaries) {
        auto voice = std::make_unique<VoragoVoice>();
        voice->setSeed(seed);  // engine order: seed first, then prepare
        voice->prepare(kSampleRate, VoragoVoiceConfig{});
        REQUIRE(voice->isPrepared());
        applyFastAttack(*voice);
        voice->setEcosystemDepth(depth);
        voice->noteOn(kNoteHz, kVelocity);

        std::uint64_t lastStep = voice->ecosystem().getControlStepCount();
        for (std::size_t i = 0; i < kTotalSamples; i += kChunk) {
            voice->processStereoBlock(l.data() + i, r.data() + i, kChunk);
            const std::uint64_t step = voice->ecosystem().getControlStepCount();
            if (step != lastStep && boundaries != nullptr) {
                boundaries->push_back(i);
            }
            lastStep = step;
        }
    };

    // Non-finite count and peak |sample| over the current l/r contents.
    auto scan = [&](std::size_t& nonFinite, float& peak) {
        nonFinite = 0;
        peak = 0.0f;
        for (std::size_t i = 0; i < kTotalSamples; ++i) {
            if (!Krate::DSP::detail::isFinite(l[i]) || !Krate::DSP::detail::isFinite(r[i])) {
                ++nonFinite;
                continue;
            }
            peak = std::max({peak, std::abs(l[i]), std::abs(r[i])});
        }
    };

    const ClickDetectorConfig cfg{.sampleRate = 48000.0f,
                                  .frameSize = 512,
                                  .hopSize = 256,
                                  .detectionThreshold = 5.0f,
                                  .energyThresholdDb = -60.0f,
                                  .mergeGap = 5};
    REQUIRE(cfg.isValid());
    ClickDetector detector(cfg);
    detector.prepare();

    // --- Arm 1: depth 1, the gated render. -----------------------------------
    std::vector<std::size_t> boundaries;
    boundaries.reserve(kTotalSamples / kChunk);
    renderArm(1.0f, &boundaries);

    std::size_t nonFinite = 0;
    float peak = 0.0f;
    scan(nonFinite, peak);

    const std::vector<ClickDetection> clicksL = detector.detect(l.data(), l.size());
    const std::vector<ClickDetection> clicksR = detector.detect(r.data(), r.size());

    // True when `idx` lies within kBoundaryRadius of a boundary (ascending list).
    auto nearBoundary = [&](std::size_t idx) {
        const auto it = std::lower_bound(boundaries.begin(), boundaries.end(), idx);
        if (it != boundaries.end() && *it - idx <= kBoundaryRadius) {
            return true;
        }
        return it != boundaries.begin() && idx - *std::prev(it) <= kBoundaryRadius;
    };

    std::size_t printed = 0;
    auto classify = [&](const std::vector<ClickDetection>& clicks, const char* ch) {
        std::size_t onBoundary = 0;
        for (const ClickDetection& c : clicks) {
            const bool isNear = nearBoundary(c.sampleIndex);
            if (isNear) {
                ++onBoundary;
            }
            if (printed < kMaxPrintedDetections) {
                std::printf("[LeverClickFreeNatural] depth1 %s click @ sample %zu t=%.4f s "
                            "amp=%.4e %s\n",
                            ch, c.sampleIndex, static_cast<double>(c.sampleIndex) / kSampleRate,
                            static_cast<double>(c.amplitude),
                            isNear ? "ON-BOUNDARY (gating)" : "off-boundary (record only)");
                ++printed;
            }
        }
        return onBoundary;
    };
    const std::size_t onBoundaryL = classify(clicksL, "L");
    const std::size_t onBoundaryR = classify(clicksR, "R");
    if (clicksL.size() + clicksR.size() > kMaxPrintedDetections) {
        std::printf("[LeverClickFreeNatural] (%zu further detections not printed)\n",
                    clicksL.size() + clicksR.size() - kMaxPrintedDetections);
    }

    // --- Arm 2: the depth-0 twin, record only. -------------------------------
    renderArm(0.0f, nullptr);
    std::size_t twinNonFinite = 0;
    float twinPeak = 0.0f;
    scan(twinNonFinite, twinPeak);
    const std::size_t twinClicksL = detector.detect(l.data(), l.size()).size();
    const std::size_t twinClicksR = detector.detect(r.data(), r.size()).size();

    std::printf("[LeverClickFreeNatural] fs=%.0f seed=0x%08X render=%zu samples (%.0f s) "
                "step boundaries=%zu\n",
                kSampleRate, static_cast<unsigned>(seed), kTotalSamples,
                static_cast<double>(kTotalSamples) / kSampleRate, boundaries.size());
    std::printf("[LeverClickFreeNatural] depth1 clicks L=%zu (on-boundary %zu, off %zu) "
                "R=%zu (on-boundary %zu, off %zu) peak=%.4f non-finite=%zu\n",
                clicksL.size(), onBoundaryL, clicksL.size() - onBoundaryL, clicksR.size(),
                onBoundaryR, clicksR.size() - onBoundaryR, static_cast<double>(peak),
                nonFinite);
    std::printf("[LeverClickFreeNatural] depth0 twin clicks L=%zu R=%zu peak=%.4f "
                "non-finite=%zu (record only)\n",
                twinClicksL, twinClicksR, static_cast<double>(twinPeak), twinNonFinite);

    // Non-vacuity: the gated render sounds, is finite, and the colony stepped.
    REQUIRE(nonFinite == 0u);
    REQUIRE(peak > 0.0f);
    REQUIRE_FALSE(boundaries.empty());
    // The gate (Clarifications Q7).
    REQUIRE(onBoundaryL == 0u);
    REQUIRE(onBoundaryR == 0u);
}
