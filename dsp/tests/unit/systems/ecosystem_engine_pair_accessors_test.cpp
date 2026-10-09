// ==============================================================================
// Layer 3: System Tests - EcosystemEngine pair accessors (specs/vorago-phase13-ui)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/vorago-phase13-ui/spec.md   (SC-019, FR-030, FR-031)
//            specs/vorago-phase13-ui/plan.md   (S2, S9.1)
//            specs/vorago-phase13-ui/tasks.md  (T002 writes this TU, T008 adds
//                                               the three getters it calls)
//
// SC-019: over a seeded 30 s run, stepped ONE simulation step at a time, the
// recorded interaction table read through getPairAgentA / getPairAgentB /
// getPairFlow equals an independent recomputation of stage 2's pair pass from
// the pre-step agent snapshot. The reference copies the engine's expressions
// and their association (ecosystem_engine.h stage 2 pair loop and
// refreshKernelDerivatives()), so both sides are IEEE-identical in this
// -fno-fast-math TU; the 1e-12 relative tolerance only absorbs a possible
// contraction difference between the inlined header and this file.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/systems/ecosystem_engine.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

using Krate::DSP::EcosystemEngine;

namespace {

/// Verbatim test-local copy of EcosystemEngine::wrapDelta (private; FR-030
/// forbids widening its access): two compares, no fmod.
double td(double d) noexcept {
    if (d > 0.5) {
        return d - 1.0;
    }
    if (d < -0.5) {
        return d + 1.0;
    }
    return d;
}

constexpr std::uint32_t kSeed = 0x5EED1234u;
constexpr double kSampleRate = 48000.0;
constexpr double kRunSeconds = 30.0;

using PairKey = std::pair<std::size_t, std::size_t>;

/// Prepare an engine exactly as the SC-019 fixture does.
void prepareFixture(EcosystemEngine& eng) {
    eng.setSeed(kSeed);
    eng.prepare(kSampleRate, EcosystemEngine::PrepareConfig{.agentCount = 32});
}

std::size_t stepSamplesOf(const EcosystemEngine& eng) {
    return eng.getStepIntervalChunks() * EcosystemEngine::kControlChunkSamples;
}

std::size_t numStepsOf(std::size_t stepSamples) {
    return static_cast<std::size_t>(
        std::ceil(kRunSeconds * kSampleRate / static_cast<double>(stepSamples)));
}

/// Advance exactly one simulation step and assert that it was exactly one.
void stepOnce(EcosystemEngine& eng, std::size_t stepSamples) {
    const std::uint64_t prev = eng.getControlStepCount();
    eng.processChunk(stepSamples);
    REQUIRE(eng.getControlStepCount() == prev + 1u);
}

}  // namespace

TEST_CASE("EcosystemEngine_PairAccessors", "[systems][ecosystem][pairs]") {
    EcosystemEngine eng;
    prepareFixture(eng);
    const std::size_t stepSamples = stepSamplesOf(eng);
    REQUIRE(stepSamples > 0u);
    const std::size_t numSteps = numStepsOf(stepSamples);
    REQUIRE(numSteps > 0u);

    SECTION("MatchesReference") {
        std::vector<double> xs;
        std::vector<double> ys;
        std::vector<double> es;
        std::size_t totalPairs = 0;

        for (std::size_t step = 0; step < numSteps; ++step) {
            // 1. Snapshot the pre-step state the pair pass reads.
            const std::size_t n = eng.getAgentCount();
            xs.assign(n, 0.0);
            ys.assign(n, 0.0);
            es.assign(n, 0.0);
            for (std::size_t i = 0; i < n; ++i) {
                xs[i] = eng.getAgentPositionX(i);
                ys[i] = eng.getAgentPositionY(i);
                es[i] = eng.getAgentEnergy(i);
            }
            const double sigma = static_cast<double>(eng.getKernelSigma());
            const double rate = static_cast<double>(eng.getExchangeRate());
            const double pred = static_cast<double>(eng.getPredation());

            // 2. One simulation step.
            stepOnce(eng, stepSamples);

            // 3. Reference - same expressions, same association as the engine
            //    (refreshKernelDerivatives() and the stage-2 pair loop).
            const double sigmaSq = sigma * sigma;
            const double twoSigSq = 2.0 * sigmaSq;
            const double inv = (twoSigSq > 0.0) ? (1.0 / twoSigSq) : 0.0;
            const double cutDistSq = twoSigSq * 13.815510557964274 * (1.0 + 1.0e-9);
            const double exchangeSign = 1.0 - (2.0 * pred);

            std::map<PairKey, double> ref;
            for (std::size_t i = 0; i < n; ++i) {
                const double xi = xs[i];
                const double yi = ys[i];
                const double ei = es[i];
                for (std::size_t j = i + 1u; j < n; ++j) {
                    const double dx = td(xs[j] - xi);
                    const double dy = td(ys[j] - yi);
                    const double d2 = (dx * dx) + (dy * dy);
                    if (d2 > cutDistSq) {
                        continue;
                    }
                    const double w = std::exp(-d2 * inv);
                    if (w < 1.0e-6) {
                        continue;
                    }
                    const double ej = es[j];
                    ref[PairKey{i, j}] = rate * w * (ej - ei) * exchangeSign;
                }
            }

            // 4. Cross-check the accessors against the reference.
            const std::size_t pairCount = eng.getPairInteractionCount();
            std::set<PairKey> got;
            std::size_t badOrder = 0;
            std::size_t badFlow = 0;
            std::size_t missing = 0;
            for (std::size_t p = 0; p < pairCount; ++p) {
                const std::size_t a = eng.getPairAgentA(p);
                const std::size_t b = eng.getPairAgentB(p);
                if (a >= b || b >= eng.getAgentCount()) {
                    ++badOrder;
                }
                got.insert(PairKey{a, b});
                const auto it = ref.find(PairKey{a, b});
                if (it == ref.end()) {
                    ++missing;
                    continue;
                }
                const double r = it->second;
                const double tol = 1e-12 * std::max(std::fabs(r), 1e-300);
                if (!(std::fabs(eng.getPairFlow(p) - r) <= tol)) {
                    ++badFlow;
                }
            }
            std::set<PairKey> refSet;
            for (const auto& kv : ref) {
                refSet.insert(kv.first);
            }

            INFO("step " << step << " of " << numSteps << ", pairCount " << pairCount
                         << ", reference pairs " << refSet.size());
            REQUIRE(badOrder == 0u);
            REQUIRE(missing == 0u);
            REQUIRE(got.size() == pairCount);  // no duplicate pair recorded
            REQUIRE(got == refSet);
            REQUIRE(badFlow == 0u);
            totalPairs += pairCount;
        }
        // The run must actually have exercised the accessors.
        REQUIRE(totalPairs > 0u);
    }

    SECTION("PredationHalf") {
        EcosystemEngine half;
        prepareFixture(half);
        half.setPredation(0.5f);
        REQUIRE(half.getPredation() == 0.5f);
        const std::size_t halfStepSamples = stepSamplesOf(half);

        bool sawPairs = false;
        std::size_t nonZero = 0;
        for (int step = 0; step < 200; ++step) {
            stepOnce(half, halfStepSamples);
            const std::size_t pairCount = half.getPairInteractionCount();
            if (pairCount > 0u) {
                sawPairs = true;
            }
            for (std::size_t p = 0; p < pairCount; ++p) {
                if (!(half.getPairFlow(p) == 0.0)) {
                    ++nonZero;
                }
            }
        }
        REQUIRE(sawPairs);
        REQUIRE(nonZero == 0u);
    }

    SECTION("OutOfRange") {
        for (int step = 0; step < 16; ++step) {
            stepOnce(eng, stepSamples);
        }
        const std::size_t pairCount = eng.getPairInteractionCount();
        const std::size_t probes[] = {pairCount, pairCount + 1u,
                                      std::numeric_limits<std::size_t>::max()};
        for (const std::size_t p : probes) {
            INFO("p = " << p << ", pairCount " << pairCount);
            REQUIRE(eng.getPairAgentA(p) == 0u);
            REQUIRE(eng.getPairAgentB(p) == 0u);
            REQUIRE(eng.getPairFlow(p) == 0.0);
        }

        EcosystemEngine unprepared;
        REQUIRE(unprepared.getPairAgentA(0) == 0u);
        REQUIRE(unprepared.getPairAgentB(0) == 0u);
        REQUIRE(unprepared.getPairFlow(0) == 0.0);
    }
}
