// ==============================================================================
// Vorago Phase 12 - parameter-step continuity (SC-011, [long])
// ==============================================================================
// T047 (specs/vorago-phase12-parameters/tasks.md), plan section 6.4. For every
// one of the 108 registered IDs (the SC-011 column of unit/param_table_expected.h):
// 48 kHz, 512-sample blocks, note C2 velocity 100 held from sample 0, 1 s warm-up,
// then 64 steps. Classes A / B: clause 1 (maxDeltaInWindow over +-10 ms centred on
// step + {0, 1024, 3072} in the output domain - spec B-6: a step lands at the
// output immediately for the output stage, after the cavern's 1024-sample
// diffusion for cavern-side controls and after the full 3072 for engine-path
// controls, so all three are measured and the max taken), clause 2 (reference windows midway between
// steps, same shift), clause 3 (max(test) <= 1.5 x max(ref)). Every class: clause 4
// (every sample finite by bit pattern, peak <= the -0.3 dBFS limiter ceiling).
// Positive controls: (a) a one-sample step of 2 x a reference window's own
// statistic must exceed the bound; (b) with FR-053's probe snapping masterGain_,
// master gain's 64-step render must FAIL clause 3.
//
// STEP GRID. The processor latches every parameter change at the START of the
// process() call that carries it (processor.cpp push step, before the first
// slice), so a step is placed on a block boundary with its point at offset 0 and
// its step sample IS that block's first sample. 125 ms (6000 samples) is not a
// multiple of 512, so the grid is 12 blocks = 6144 samples = 128 ms: each
// reference window centre then sits 64 ms from both neighbouring steps (54 ms
// clear of the window edge; the spec asks >= 50 ms). Warm-up = 94 blocks
// (48128 samples, >= 1 s).
//
// Class A starts at normalized 0 (set at sample 0, before warm-up) and moves in
// 64 equal normalized steps of 1/64 to 1. Classes B / C warm up at the default
// index and step through kDiscreteStepSeq (seed 12011); for 320-323 the slot's
// model (310 + k) is set to Direct at sample 0.
// ==============================================================================

#include "plugin_ids.h"
#include "unit/param_table_expected.h"
#include "vorago_test_fixture.h"

#include "parameters/param_mapping.h"
#include "processor/processor.h"

#include <vorago_fixtures.h>
#include <vst_event_list.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <span>
#include <vector>

// -----------------------------------------------------------------------------
// FR-053 probe: DEFINED only here (declared in processor.h, a friend of
// Processor). Engaging it makes pushGlobalParams() snapTo the master gain.
// -----------------------------------------------------------------------------
namespace Vorago::detail {
struct VoragoMasterGainSmootherBypassProbe {
    static void engage(Processor& p) noexcept { p.masterGainSnapProbe_ = true; }
};
}  // namespace Vorago::detail

namespace {

constexpr double kSampleRate = 48000.0;
constexpr std::size_t kBlock = 512;
constexpr std::size_t kSteps = 64;
constexpr std::size_t kWarmupBlocks = 94;                          // 48128 samples >= 1 s
constexpr std::size_t kStepBlocks = 27;                            // 13824 samples = 288 ms (B-6)
constexpr std::size_t kFirstStep = kWarmupBlocks * kBlock;         // 48128
constexpr std::size_t kStepSpacing = kStepBlocks * kBlock;         // 6144
constexpr std::size_t kLatency = 3072;                             // FR-033: smear 2048 + cavern 1024
constexpr std::size_t kCavernLatency = 1024;                       // cavern diffusion alone
// B-6: the three output-domain offsets a parameter step can land at.
constexpr std::array<std::size_t, 3> kOffsets{{0u, kCavernLatency, kLatency}};
constexpr std::size_t kHalfWindow = 480;                           // 10 ms at 48 kHz
constexpr std::size_t kTotalBlocks = kWarmupBlocks + kSteps * kStepBlocks + 1;  // 1823
constexpr std::size_t kTotalSamples = kTotalBlocks * kBlock;                    // 933376
constexpr float kOutputCeiling = 0.9661f;  // 10^(-0.3/20) = 0.96605, vorago_engine.h:203
constexpr double kBoundFactor = 1.5;
constexpr Steinberg::int16 kNotePitch = 36;  // C2
constexpr float kVelocity = 100.0f / 127.0f;

// The last reference window (centre = last step + spacing / 2 + latency) ends
// inside the render.
static_assert(kFirstStep + (kSteps - 1) * kStepSpacing + kStepSpacing / 2 + kLatency +
                  kHalfWindow <=
              kTotalSamples);
// Every reference window (midpoint + {0, 1024, 3072}, +-480) lies >= 50 ms (2400
// samples) clear of the preceding step's last test window and of the next step.
static_assert(kStepSpacing / 2 - kHalfWindow - (kLatency + kHalfWindow) >= 2400);
static_assert(kStepSpacing - (kStepSpacing / 2 + kLatency + kHalfWindow) >= 2400);

// -----------------------------------------------------------------------------
// kDiscreteStepSeq: checked-in, generated ONCE by a Node one-off (mulberry32,
// seed 12011, one stream per count n; the first index is drawn from [0, n), each
// later one from the n - 1 values that differ from its predecessor). Keyed by the
// list's value count.
// -----------------------------------------------------------------------------
using Seq = std::array<int, kSteps>;

// clang-format off
constexpr Seq kDiscreteStepSeq2{0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1};
constexpr Seq kDiscreteStepSeq3{0, 1, 0, 2, 0, 2, 0, 1, 2, 1, 2, 1, 0, 2, 1, 2, 0, 2, 1, 2, 1, 2, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 2, 1, 2, 1, 0, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 2, 1, 0, 1, 0, 2, 1, 2, 1, 0, 1, 0, 1, 0, 2, 0};
constexpr Seq kDiscreteStepSeq4{0, 1, 0, 3, 1, 3, 0, 1, 3, 1, 3, 2, 0, 3, 2, 3, 1, 2, 1, 3, 2, 3, 2, 0, 1, 0, 2, 0, 2, 1, 0, 2, 3, 2, 1, 2, 0, 3, 0, 1, 2, 0, 1, 3, 0, 1, 2, 0, 3, 2, 0, 2, 1, 2, 3, 2, 1, 0, 1, 0, 1, 0, 2, 0};
constexpr Seq kDiscreteStepSeq6{0, 1, 2, 4, 2, 5, 0, 2, 5, 3, 4, 5, 1, 5, 3, 5, 1, 3, 4, 5, 3, 5, 4, 1, 2, 0, 2, 1, 2, 1, 0, 2, 4, 5, 2, 4, 0, 4, 0, 1, 3, 0, 2, 5, 1, 0, 4, 1, 4, 5, 1, 3, 2, 4, 5, 4, 2, 1, 0, 1, 0, 1, 3, 0};
constexpr Seq kDiscreteStepSeq11{1, 2, 3, 7, 4, 10, 0, 3, 10, 6, 7, 10, 2, 9, 6, 10, 3, 6, 7, 9, 7, 9, 8, 2, 3, 1, 4, 2, 4, 3, 1, 4, 8, 10, 5, 7, 1, 8, 1, 0, 6, 1, 3, 10, 2, 1, 7, 2, 7, 9, 2, 5, 4, 7, 10, 8, 5, 2, 0, 2, 1, 0, 6, 0};
constexpr Seq kDiscreteStepSeq12{1, 2, 3, 8, 5, 11, 0, 3, 11, 7, 8, 11, 2, 10, 7, 11, 4, 6, 7, 9, 8, 10, 9, 2, 4, 2, 4, 3, 5, 3, 1, 5, 9, 11, 5, 8, 1, 9, 2, 0, 6, 1, 4, 11, 2, 1, 7, 2, 8, 10, 3, 5, 4, 8, 10, 9, 6, 2, 0, 2, 1, 0, 7, 0};
constexpr Seq kDiscreteStepSeq16{2, 3, 5, 11, 6, 15, 1, 4, 14, 9, 11, 15, 3, 13, 10, 15, 5, 8, 10, 13, 11, 13, 14, 3, 5, 2, 6, 4, 6, 5, 2, 6, 12, 15, 7, 10, 1, 11, 2, 0, 8, 1, 5, 15, 3, 1, 10, 3, 11, 13, 4, 7, 6, 10, 14, 13, 8, 4, 0, 2, 3, 0, 9, 0};
// clang-format on

[[nodiscard]] constexpr bool isValidSeq(const Seq& s, int n) noexcept {
    for (std::size_t k = 0; k < s.size(); ++k) {
        if (s[k] < 0 || s[k] >= n) { return false; }
        if (k > 0 && s[k] == s[k - 1]) { return false; }
    }
    return true;
}

/// Plan 6.4: ID 403's sequence (3 values) contains all six ordered pairs.
[[nodiscard]] constexpr bool hasAllSixPairs(const Seq& s) noexcept {
    std::array<bool, 9> seen{};
    for (std::size_t k = 1; k < s.size(); ++k) {
        seen[(static_cast<std::size_t>(s[k - 1]) * 3u) + static_cast<std::size_t>(s[k])] = true;
    }
    for (int a = 0; a < 3; ++a) {
        for (int b = 0; b < 3; ++b) {
            if (a != b && !seen[(static_cast<std::size_t>(a) * 3u) + static_cast<std::size_t>(b)]) { return false; }
        }
    }
    return true;
}

static_assert(isValidSeq(kDiscreteStepSeq2, 2));
static_assert(isValidSeq(kDiscreteStepSeq3, 3));
static_assert(isValidSeq(kDiscreteStepSeq4, 4));
static_assert(isValidSeq(kDiscreteStepSeq6, 6));
static_assert(isValidSeq(kDiscreteStepSeq11, 11));
static_assert(isValidSeq(kDiscreteStepSeq12, 12));
static_assert(isValidSeq(kDiscreteStepSeq16, 16));
static_assert(hasAllSixPairs(kDiscreteStepSeq3));

// Scope = the SC-011 column of the checked-in table (plan 6.4).
static_assert(VoragoTest::kNumExpectedParams == 108);
static_assert(VoragoTest::detail_expected::countClass(VoragoTest::Sc011::A) == 85);
static_assert(VoragoTest::detail_expected::countClass(VoragoTest::Sc011::B) == 12);
static_assert(VoragoTest::detail_expected::countClass(VoragoTest::Sc011::C) == 11);

[[nodiscard]] const Seq* seqForCount(int n) noexcept {
    switch (n) {
        case 2: return &kDiscreteStepSeq2;
        case 3: return &kDiscreteStepSeq3;
        case 4: return &kDiscreteStepSeq4;
        case 6: return &kDiscreteStepSeq6;
        case 11: return &kDiscreteStepSeq11;
        case 12: return &kDiscreteStepSeq12;
        case 16: return &kDiscreteStepSeq16;
        default: return nullptr;
    }
}

/// Value count of a stepped row. Sustain pedal (4) is tabulated Linear [0, 1]
/// but is an on/off gate (>= 0.5 = down): stepped as a 2-value list.
[[nodiscard]] int valueCountOf(const VoragoTest::ExpectedParamRow& row) noexcept {
    return (row.taper == ::Vorago::Taper::Discrete) ? static_cast<int>(row.stepCount) + 1 : 2;
}

/// The 64 normalized values the sweep sends, step k at kFirstStep + k * spacing.
[[nodiscard]] std::array<double, kSteps> stepValues(const VoragoTest::ExpectedParamRow& row) {
    std::array<double, kSteps> v{};
    if (row.sc011 == VoragoTest::Sc011::A) {
        for (std::size_t k = 0; k < kSteps; ++k) {
            v[k] = static_cast<double>(k + 1) / static_cast<double>(kSteps);
        }
        return v;
    }
    const int n = valueCountOf(row);
    const Seq* seq = seqForCount(n);
    INFO("ID " << row.id << " value count " << n);
    REQUIRE(seq != nullptr);
    const int def = ::Vorago::indexFromNormalized(row.defaultNormalized, n);
    // Step 0 must be a real change from the warm-up (default) index: rotate the
    // sequence by one when it starts on the default. A rotation keeps "no index
    // equal to its predecessor" and maps the ordered-pair set onto itself.
    const int rot = ((*seq)[0] == def) ? 1 : 0;
    for (std::size_t k = 0; k < kSteps; ++k) {
        v[k] = ::Vorago::indexToNormalized(((*seq)[k] + rot) % n, n);
    }
    return v;
}

struct SweepResult {
    std::array<double, kSteps> test{};   ///< clause 1, max over step + kOffsets
    std::array<double, kSteps> ref{};    ///< clause 2, max over step + spacing / 2 + kOffsets
    bool finite = true;
    float peak = 0.0f;
    std::vector<float> maxRefWindow;  ///< the reference window (channel) holding max(ref)
};

[[nodiscard]] double windowStat(std::span<const float> x, std::size_t centre) {
    return Krate::DSP::TestUtils::Vorago::maxDeltaInWindow(
        x.subspan(centre - kHalfWindow, 2u * kHalfWindow), 0);
}

[[nodiscard]] SweepResult renderSweep(const VoragoTest::ExpectedParamRow& row, bool probe) {
    VoragoTest::ProcessorFixture fx;
    fx.prepare(kSampleRate, 2048);
    if (probe) {
        ::Vorago::detail::VoragoMasterGainSmootherBypassProbe::engage(*fx.proc);
    }
    fx.reserveCapture(kTotalSamples);

    const std::array<double, kSteps> values = stepValues(row);
    const bool isNoiseType = row.id >= ::Vorago::kNoiseSlot0TypeId &&
                             row.id <= ::Vorago::kNoiseSlot3TypeId;

    VoragoTest::MultiParamChanges pc;
    pc.reserve(2);
    Krate::Test::EventList ev;

    for (std::size_t b = 0; b < kTotalBlocks; ++b) {
        pc.clear();
        ev.clear();
        const std::size_t start = b * kBlock;
        if (b == 0) {
            ev.addNoteOn(kNotePitch, kVelocity, 0);
            if (row.sc011 == VoragoTest::Sc011::A) {
                pc.addQueue(row.id).addTestPoint(0, 0.0);  // start at the min extreme
            }
            if (isNoiseType) {  // the type is audible only on a Direct slot (index 0)
                const Steinberg::Vst::ParamID modelId =
                    ::Vorago::kNoiseSlot0ModelId + (row.id - ::Vorago::kNoiseSlot0TypeId);
                pc.addQueue(modelId).addTestPoint(0, ::Vorago::indexToNormalized(0, 4));
            }
        }
        if (start >= kFirstStep && (start - kFirstStep) % kStepSpacing == 0) {
            const std::size_t k = (start - kFirstStep) / kStepSpacing;
            if (k < kSteps) {
                pc.addQueue(row.id).addTestPoint(0, values[k]);
            }
        }
        REQUIRE(fx.processBlock(kBlock, &ev, &pc) == Steinberg::kResultOk);
    }

    SweepResult r;
    const std::span<const float> l(fx.capturedL);
    const std::span<const float> rr(fx.capturedR);
    REQUIRE(l.size() == kTotalSamples);
    r.finite = VoragoTest::allFinite(l) && VoragoTest::allFinite(rr);
    r.peak = std::max(VoragoTest::peakOf(l), VoragoTest::peakOf(rr));

    double bestRef = -1.0;
    for (std::size_t k = 0; k < kSteps; ++k) {
        const std::size_t step = kFirstStep + k * kStepSpacing;
        const std::size_t mid = step + kStepSpacing / 2;
        r.test[k] = 0.0;
        r.ref[k] = 0.0;
        for (const std::size_t off : kOffsets) {  // B-6: same three draws on both sides
            const std::size_t testCentre = step + off;
            const std::size_t refCentre = mid + off;
            r.test[k] = std::max({r.test[k], windowStat(l, testCentre), windowStat(rr, testCentre)});
            const double refL = windowStat(l, refCentre);
            const double refR = windowStat(rr, refCentre);
            r.ref[k] = std::max({r.ref[k], refL, refR});
            const double refHere = std::max(refL, refR);
            if (refHere > bestRef) {
                bestRef = refHere;
                const std::span<const float> src = (refL >= refR) ? l : rr;
                const std::span<const float> w =
                    src.subspan(refCentre - kHalfWindow, 2u * kHalfWindow);
                r.maxRefWindow.assign(w.begin(), w.end());
            }
        }
    }
    return r;
}

[[nodiscard]] double maxOf(const std::array<double, kSteps>& a) noexcept {
    return *std::max_element(a.begin(), a.end());
}

[[nodiscard]] const char* className(VoragoTest::Sc011 c) noexcept {
    switch (c) {
        case VoragoTest::Sc011::A: return "A";
        case VoragoTest::Sc011::B: return "B";
        case VoragoTest::Sc011::C: return "C";
    }
    return "?";
}

}  // namespace

TEST_CASE("Vorago_ParameterStepsAreContinuous", "[vorago][integration][long]") {
    std::printf("SC-011 continuity (B-6 geometry): %zu IDs, %zu steps every %zu samples, "
                "windows +-%zu at step + {0, %zu, %zu} and the same three at the midpoint, "
                "bound %.2f x max(ref), ceiling %.4f\n",
                VoragoTest::kNumExpectedParams, kSteps, kStepSpacing, kHalfWindow, kCavernLatency,
                kLatency, kBoundFactor, static_cast<double>(kOutputCeiling));
    std::printf("%6s %2s %12s %12s %8s %6s %9s %s\n", "id", "cl", "max(test)", "max(ref)",
                "ratio", "finite", "peak", "verdict");

    std::vector<Steinberg::Vst::ParamID> failing;
    SweepResult masterGain;
    double masterGainMaxRef = 0.0;

    for (const VoragoTest::ExpectedParamRow& row : VoragoTest::kExpectedParams) {
        const SweepResult r = renderSweep(row, false);
        const double maxTest = maxOf(r.test);
        const double maxRef = maxOf(r.ref);
        const double ratio = (maxRef > 0.0) ? maxTest / maxRef : 0.0;
        const bool measured = row.sc011 != VoragoTest::Sc011::C;
        const bool clause3 = !measured || (maxRef > 0.0 && maxTest <= kBoundFactor * maxRef);
        const bool clause4 = r.finite && r.peak <= kOutputCeiling;
        const bool pass = clause3 && clause4;

        std::printf("%6u %2s %12.6g %12.6g %8.4f %6s %9.6f %s\n",
                    static_cast<unsigned>(row.id), className(row.sc011), maxTest, maxRef,
                    ratio, r.finite ? "yes" : "NO",
                    static_cast<double>(r.peak), pass ? "PASS" : "FAIL");
        if (!pass) {
            failing.push_back(row.id);
        }

        INFO("ID " << row.id << " (" << row.title << ") class " << className(row.sc011)
                   << ": max(test)=" << maxTest << " max(ref)=" << maxRef << " ratio=" << ratio
                   << " peak=" << r.peak);
        CHECK(r.finite);                  // clause 4
        CHECK(r.peak <= kOutputCeiling);  // clause 4
        if (measured) {
            CHECK(maxRef > 0.0);                       // non-vacuity: the render is audible
            CHECK(maxTest <= kBoundFactor * maxRef);   // clause 3
        }

        if (row.id == ::Vorago::kMasterGainId) {
            masterGain = r;
            masterGainMaxRef = maxRef;
        }
    }

    std::printf("failing IDs (hand to T048):");
    for (const Steinberg::Vst::ParamID id : failing) {
        std::printf(" %u", static_cast<unsigned>(id));
    }
    std::printf("%s\n", failing.empty() ? " none" : "");

    // ---- Positive control (a): detector wiring ----
    {
        std::vector<float> w = masterGain.maxRefWindow;
        REQUIRE(w.size() == 2u * kHalfWindow);
        REQUIRE(masterGainMaxRef > 0.0);
        const double own = Krate::DSP::TestUtils::Vorago::maxDeltaInWindow(w, 0);
        const std::size_t at = w.size() / 2u;
        // Same sign as the local slope, so |delta| at `at` = |d| + 2 x own >= 2 x own.
        const float sign = (w[at] - w[at - 1u] >= 0.0f) ? 1.0f : -1.0f;
        const auto jump = static_cast<float>(2.0 * own) * sign;
        for (std::size_t i = at; i < w.size(); ++i) {
            w[i] += jump;
        }
        const double injected = Krate::DSP::TestUtils::Vorago::maxDeltaInWindow(w, 0);
        std::printf("control (a): window own=%.6g injected=%.6g bound=%.6g -> %s\n", own,
                    injected, kBoundFactor * masterGainMaxRef,
                    injected > kBoundFactor * masterGainMaxRef ? "exceeds (OK)" : "DOES NOT EXCEED");
        INFO("control (a): own=" << own << " injected=" << injected
                                 << " bound=" << kBoundFactor * masterGainMaxRef);
        CHECK(injected > kBoundFactor * masterGainMaxRef);
    }

    // ---- Positive control (b): criterion wiring (FR-053 probe engaged) ----
    {
        const VoragoTest::ExpectedParamRow* row =
            VoragoTest::detail_expected::findRow(::Vorago::kMasterGainId);
        REQUIRE(row != nullptr);
        const SweepResult r = renderSweep(*row, true);
        const double maxTest = maxOf(r.test);
        const double maxRef = maxOf(r.ref);
        const bool failsClause3 = maxTest > kBoundFactor * maxRef;
        std::printf("control (b): snapped master gain max(test)=%.6g max(ref)=%.6g ratio=%.4f "
                    "-> %s\n",
                    maxTest, maxRef, (maxRef > 0.0) ? maxTest / maxRef : 0.0,
                    failsClause3 ? "fails clause 3 (OK)" : "PASSES clause 3 (control broken)");
        INFO("control (b): max(test)=" << maxTest << " max(ref)=" << maxRef);
        CHECK(failsClause3);
    }
}
