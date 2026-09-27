// ==============================================================================
// Vorago Phase 13 - EcosystemFrame builder unit tests (T009)
// ==============================================================================
// Reference: specs/vorago-phase13-ui/spec.md  (FR-020, FR-020a, FR-027, C-2)
//            specs/vorago-phase13-ui/plan.md  (S3.1, S3.2, S9.2)
//
// Vorago_EcosystemFrameLayout  - FR-020: the header's static_asserts mirrored
//                                at run time so the count reaches the report.
// Vorago_EcosystemEnergyGlow   - SC-006 function arm: share / (1 + share).
// Vorago_SanitizeFrameFloat    - SC-011 function arm, FR-027: non-finite and
//                                out-of-float-range doubles -> exactly 0.0f.
// Vorago_SelectStrongestLinks  - SC-007 (b)(c)(d): the carried links are the
//                                64 strongest non-zero recorded flows.
//
// This TU is compiled with -fno-fast-math (T020) so the NaN/Inf inputs built
// from bit patterns stay what they are, and every float compare is IEEE.
// No bit-exact golden is stored: every compare is against a live value
// recomputed in this binary.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "processor/ecosystem_frame.h"
#include "processor/ecosystem_frame_builder.h"

#include <krate/dsp/systems/ecosystem_engine.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

using Krate::DSP::EcosystemEngine;
using Vorago::EcosystemFrame;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr std::uint32_t kSeed = 0x5EED1234u;

/// Build a double from its bit pattern through a volatile read so the
/// optimiser cannot fold the non-finite value away.
double doubleFromBits(std::uint64_t bits) noexcept {
    volatile std::uint64_t b = bits;
    return std::bit_cast<double>(static_cast<std::uint64_t>(b));
}

std::size_t stepSamplesOf(const EcosystemEngine& eng) {
    return eng.getStepIntervalChunks() * EcosystemEngine::kControlChunkSamples;
}

/// Advance exactly one simulation step and assert that it was exactly one
/// (the SC-019 / plan S9.1 stepping).
void stepOnce(EcosystemEngine& eng, std::size_t stepSamples) {
    const std::uint64_t prev = eng.getControlStepCount();
    eng.processChunk(stepSamples);
    REQUIRE(eng.getControlStepCount() == prev + 1u);
}

/// Invariants every arm asserts (plan S9.2): indices in range, no self link,
/// every entry at or above linkCount zero.
void checkLinkInvariants(const EcosystemFrame& f, std::size_t agentCount) {
    const std::size_t k = f.linkCount;
    REQUIRE(k <= Vorago::kMaxFrameLinks);
    for (std::size_t l = 0; l < k; ++l) {
        REQUIRE(static_cast<std::size_t>(f.linkA[l]) < agentCount);
        REQUIRE(static_cast<std::size_t>(f.linkB[l]) < agentCount);
        REQUIRE(f.linkA[l] != f.linkB[l]);
    }
    for (std::size_t l = k; l < Vorago::kMaxFrameLinks; ++l) {
        REQUIRE(f.linkA[l] == 0);
        REQUIRE(f.linkB[l] == 0);
        REQUIRE(f.linkStrength[l] == 0.0f);
    }
}

/// Pre-dirty the link arrays so the zero-fill of [linkCount, 64) is observed,
/// not inherited from a value-initialised frame.
void dirtyLinks(EcosystemFrame& f) {
    f.linkCount = 0xFF;
    for (std::size_t l = 0; l < Vorago::kMaxFrameLinks; ++l) {
        f.linkA[l] = 0xAB;
        f.linkB[l] = 0xCD;
        f.linkStrength[l] = 123.0f;
    }
}

}  // namespace

// -----------------------------------------------------------------------------
// FR-020: runtime mirror of the ecosystem_frame.h static_asserts.
// -----------------------------------------------------------------------------
TEST_CASE("Vorago_EcosystemFrameLayout", "[vorago][ecosystem][builder]") {
    REQUIRE(sizeof(EcosystemFrame) == 1072u);
    REQUIRE(offsetof(EcosystemFrame, linkStrength) == 812u);
    REQUIRE(offsetof(EcosystemFrame, linkFlowScale) == 1068u);
    REQUIRE(std::is_trivially_copyable_v<EcosystemFrame>);
    REQUIRE(std::is_standard_layout_v<EcosystemFrame>);
    REQUIRE(Vorago::kMaxFrameAgents == EcosystemEngine::kMaxAgents);
}

// -----------------------------------------------------------------------------
// SC-006 function arm: glow = share / (1 + share), share = e * n / budget.
// -----------------------------------------------------------------------------
TEST_CASE("Vorago_EcosystemEnergyGlow", "[vorago][ecosystem][builder]") {
    constexpr std::size_t kAgents = 32;
    constexpr double kBudget = 1.0;
    constexpr std::array<double, 8> shares{0.0, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 48.0};

    std::array<float, shares.size()> glow{};
    for (std::size_t s = 0; s < shares.size(); ++s) {
        const double energy = shares[s] / static_cast<double>(kAgents);
        glow[s] = Vorago::ecosystemEnergyGlow(energy, kAgents, kBudget);
        INFO("share " << shares[s] << " -> glow " << glow[s]);
        REQUIRE(glow[s] >= 0.0f);
        REQUIRE(glow[s] < 1.0f);
    }

    REQUIRE(glow[0] == 0.0f);
    for (std::size_t s = 1; s < shares.size(); ++s) {
        INFO("share " << shares[s - 1] << " -> " << shares[s]);
        REQUIRE(glow[s] > glow[s - 1]);  // strictly increasing after share 0
    }
    REQUIRE(glow[2] == 0.5f);  // share 1
    REQUIRE(glow[7] < 1.0f);   // share 48

    // Negative share -> 0.
    REQUIRE(Vorago::ecosystemEnergyGlow(-1.0 / static_cast<double>(kAgents), kAgents, kBudget)
            == 0.0f);

    // Quiet-NaN energy -> 0.
    const double qnan = doubleFromBits(0x7FF8000000000000ull);
    REQUIRE(Vorago::ecosystemEnergyGlow(qnan, kAgents, kBudget) == 0.0f);
}

// -----------------------------------------------------------------------------
// SC-011 function arm, FR-027.
// -----------------------------------------------------------------------------
TEST_CASE("Vorago_SanitizeFrameFloat", "[vorago][ecosystem][builder]") {
    SECTION("NonFiniteAndOutOfRangeBecomeZero") {
        const std::array<std::pair<const char*, double>, 6> zeroed{{
            {"qNaN", doubleFromBits(0x7FF8000000000000ull)},
            {"sNaN", doubleFromBits(0x7FF0000000000001ull)},
            {"+Inf", doubleFromBits(0x7FF0000000000000ull)},
            {"-Inf", doubleFromBits(0xFFF0000000000000ull)},
            {"1e300", 1e300},
            {"-1e300", -1e300},
        }};
        for (const auto& [name, v] : zeroed) {
            INFO(name);
            const float out = Vorago::sanitizeFrameFloat(v);
            REQUIRE(std::bit_cast<std::uint32_t>(out) == std::bit_cast<std::uint32_t>(0.0f));
        }
    }

    SECTION("FiniteInRangeNarrows") {
        const std::array<double, 3> kept{0.25, -3.5, static_cast<double>(FLT_MAX)};
        for (const double v : kept) {
            INFO("v = " << v);
            REQUIRE(Vorago::sanitizeFrameFloat(v) == static_cast<float>(v));
        }
    }
}

// -----------------------------------------------------------------------------
// SC-007 (b)(c)(d).
// -----------------------------------------------------------------------------
TEST_CASE("Vorago_SelectStrongestLinks", "[vorago][ecosystem][builder]") {
    std::array<std::uint16_t, EcosystemEngine::kMaxPairs> scratch{};

    SECTION("FullTableCarriesTheSixtyFourStrongest") {  // (b)
        auto eng = std::make_unique<EcosystemEngine>();
        eng->setSeed(kSeed);
        eng->setKernelSigma(0.35f);
        eng->prepare(kSampleRate, EcosystemEngine::PrepareConfig{.agentCount = 48});
        REQUIRE(eng->getAgentCount() == 48u);
        const std::size_t stepSamples = stepSamplesOf(*eng);

        bool sawFullTable = false;
        bool sawFullTableWithSixtyFourLinks = false;
        constexpr int kSteps = 200;
        for (int step = 0; step < kSteps; ++step) {
            stepOnce(*eng, stepSamples);
            const std::size_t n = eng->getPairInteractionCount();
            if (n == EcosystemEngine::kMaxPairs) {
                sawFullTable = true;
            }

            // Reference: |flow| of every recorded pair, zeros dropped, sorted
            // descending, first 64 taken.
            std::map<std::pair<std::size_t, std::size_t>, double> absFlowOf;
            std::vector<double> ref;
            ref.reserve(n);
            for (std::size_t p = 0; p < n; ++p) {
                const double a = std::fabs(eng->getPairFlow(p));
                absFlowOf[{eng->getPairAgentA(p), eng->getPairAgentB(p)}] = a;
                if (a != 0.0) {
                    ref.push_back(a);
                }
            }
            std::sort(ref.begin(), ref.end(), std::greater<>());
            const std::size_t expectedCount = std::min(ref.size(), Vorago::kMaxFrameLinks);
            ref.resize(expectedCount);

            auto frame = std::make_unique<EcosystemFrame>();
            dirtyLinks(*frame);
            Vorago::selectStrongestLinks(*eng, scratch, *frame);

            INFO("step " << step << ", pairs " << n << ", non-zero " << ref.size());
            REQUIRE(static_cast<std::size_t>(frame->linkCount) == expectedCount);
            checkLinkInvariants(*frame, eng->getAgentCount());

            // Multiset of carried strengths == multiset of narrowed reference.
            std::vector<float> got(frame->linkStrength, frame->linkStrength + frame->linkCount);
            std::vector<float> want;
            want.reserve(ref.size());
            for (const double a : ref) {
                want.push_back(static_cast<float>(a));
            }
            std::sort(got.begin(), got.end());
            std::sort(want.begin(), want.end());
            REQUIRE(got == want);

            // Every carried (A, B) is a recorded pair, carried once, whose
            // |flow| narrows to exactly its strength.
            std::vector<std::pair<std::size_t, std::size_t>> seen;
            for (std::size_t l = 0; l < frame->linkCount; ++l) {
                const std::pair<std::size_t, std::size_t> key{frame->linkA[l], frame->linkB[l]};
                const auto it = absFlowOf.find(key);
                REQUIRE(it != absFlowOf.end());
                REQUIRE(static_cast<float>(it->second) == frame->linkStrength[l]);
                REQUIRE(std::find(seen.begin(), seen.end(), key) == seen.end());
                seen.push_back(key);
            }

            if (n == EcosystemEngine::kMaxPairs
                && static_cast<std::size_t>(frame->linkCount) == Vorago::kMaxFrameLinks) {
                sawFullTableWithSixtyFourLinks = true;
            }
        }
        REQUIRE(sawFullTable);                    // the 1128-pair arm ran
        REQUIRE(sawFullTableWithSixtyFourLinks);  // and carried linkCount == 64
    }

    SECTION("PredationHalfCarriesNoLinks") {  // (c)
        auto eng = std::make_unique<EcosystemEngine>();
        eng->setSeed(kSeed);
        eng->prepare(kSampleRate, EcosystemEngine::PrepareConfig{.agentCount = 32});
        eng->setPredation(0.5f);
        const std::size_t stepSamples = stepSamplesOf(*eng);

        bool sawPairs = false;
        for (int step = 0; step < 200; ++step) {
            stepOnce(*eng, stepSamples);
            if (eng->getPairInteractionCount() > 0) {
                sawPairs = true;
            }
            auto frame = std::make_unique<EcosystemFrame>();
            dirtyLinks(*frame);
            Vorago::selectStrongestLinks(*eng, scratch, *frame);
            INFO("step " << step);
            REQUIRE(frame->linkCount == 0);
            checkLinkInvariants(*frame, eng->getAgentCount());
        }
        REQUIRE(sawPairs);  // non-vacuity: pairs were recorded, all with flow 0
    }

    SECTION("SingleAgentCarriesNoLinks") {  // (d)
        auto eng = std::make_unique<EcosystemEngine>();
        eng->setSeed(kSeed);
        eng->prepare(kSampleRate, EcosystemEngine::PrepareConfig{.agentCount = 1});
        REQUIRE(eng->getAgentCount() == 1u);
        const std::size_t stepSamples = stepSamplesOf(*eng);

        for (int step = 0; step < 50; ++step) {
            stepOnce(*eng, stepSamples);
            auto frame = std::make_unique<EcosystemFrame>();
            dirtyLinks(*frame);
            Vorago::selectStrongestLinks(*eng, scratch, *frame);
            INFO("step " << step);
            REQUIRE(frame->linkCount == 0);
            checkLinkInvariants(*frame, eng->getAgentCount());
        }
    }
}
