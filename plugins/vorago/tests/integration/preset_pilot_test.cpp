// ==============================================================================
// Vorago Phase 14 - E0 take curve and pilot calibration; FR-017a; filled by T007, T038
// ==============================================================================
// Registered by T003 (specs/vorago-phase14-presets-release/tasks.md).
//
// T007 - Vorago_PresetPilot_DefaultSurfaceTakeCurve (E0, plan 6.16 step 1).
// HIDDEN ([.probe]) and run once by hand, alone (T008). The TU IS the
// measurement: the default surface (no setState) rendered at all 16 seed
// indices (A_8 u B_8) with the C-6 stimulus (NoteOn 36, velocity 100/127,
// sample 0; 512-sample blocks; 48 kHz), captured over M1..M3.
//
// Hard-coded default-surface timeline (makeTimeline does not exist until C2):
// the shipped stage times {20000, 30000, 45000, 60000} ms, Standard mode
// (vorago_voice.h:324-328, kDefaultStageTimesMs), so
//   A = 155 s; M1 = [160, 220] s; M2 = [220, 280] s; M3 = [280, 340] s;
//   end = H = 340 s. No NoteOff, no tail.
//
// For K in {1, 2, 4, 8}: D_A = meanOf the 3K minute descriptors of the seeds
// takeSeedIndex(0, 0, j, K), D_B likewise for set 1, t_K = d(D_A, D_B).
// Printed: each seed's stereo M1 RMS, the t_K table, the t_1 cross-check
// against Phase 13b's t0on = 5.9515 (final2_table_default.log:43; tolerance
// 0.0015 = 5e-5 print rounding + kMetricTolerance 2.5e-4 x 5.95), and the
// FR-017a verdict line. Verdicts are read from the log, never asserted.
//
// Assertions (only): every render finite by bit pattern with peak <= 0.9661;
// every seed's M1 stereo RMS >= -60 dBFS; the pool trust check (plan 11): seed 0
// rendered once more SERIALLY matches the pooled seed-0 M1 per channel within
// compareFingerprints tolerance.
//
// THREADING: renders run through VoragoTest::runJobs; the descriptor math is
// NOT thread-safe (low_frequency_metrics.h:95-134 keeps function-local static
// FFT state), so describe() runs on the test thread. Renders go in batches of
// the pool width and each batch's captures are released after description,
// keeping peak memory at <= width x ~69 MB.
//
// NAMESPACE HAZARD: Krate::DSP::TestUtils::Vorago exists, so no `using
// namespace` appears in this TU.
// ==============================================================================

#include "preset_test_support.h"

#include <render_fingerprint.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

// ---- Hard-coded default-surface timeline (plan 6.16 step 1) --------------------
constexpr double kE0SampleRate = 48000.0;
constexpr double kE0AttackSeconds = 155.0;  // (20000 + 30000 + 45000 + 60000) ms
constexpr std::array<std::pair<double, double>, 3> kE0Minutes{{
    {kE0AttackSeconds + 5.0, kE0AttackSeconds + 65.0},    // M1 = [160, 220]
    {kE0AttackSeconds + 65.0, kE0AttackSeconds + 125.0},  // M2 = [220, 280]
    {kE0AttackSeconds + 125.0, kE0AttackSeconds + 185.0}, // M3 = [280, 340]
}};
constexpr double kE0EndSeconds = kE0AttackSeconds + 185.0;  // H = 340 s

constexpr float kE0OutputCeiling = 0.9661f;  // soak_test.cpp:45
constexpr double kE0MinM1RmsDb = -60.0;

// ---- Phase 13b cross-check (final2_table_default.log:43) -----------------------
constexpr double kE0T0On13b = 5.9515;
constexpr double kE0T1Tolerance = 0.0015;

constexpr std::array<int, 4> kE0Takes{1, 2, 4, 8};

/// Per-seed result, filled on the test thread after the seed's render.
struct E0SeedResult {
    bool finite = false;
    float peak = 0.0f;
    double m1RmsDb = -240.0;
    std::array<VoragoTest::PresetDescriptor, 3> minutes{};
};

VoragoTest::RenderSpec e0Spec(int seedIndex) {
    VoragoTest::RenderSpec spec;
    spec.seedIndex = seedIndex;
    spec.sr = kE0SampleRate;
    spec.end = kE0EndSeconds;
    spec.capture.assign(kE0Minutes.begin(), kE0Minutes.end());
    return spec;
}

// Stereo power of a span in dB: (sum L^2 + sum R^2) / (2n) (the probe's
// stereoRmsDb, ecosystem_rule_probe_test.cpp:339-350).
double e0StereoRmsDb(std::span<const float> l, std::span<const float> r) {
    double sum = 0.0;
    for (const float s : l) {
        sum += static_cast<double>(s) * static_cast<double>(s);
    }
    for (const float s : r) {
        sum += static_cast<double>(s) * static_cast<double>(s);
    }
    const double p = sum / (2.0 * static_cast<double>(std::max<std::size_t>(l.size(), 1u)));
    return 10.0 * std::log10(std::max(p, 1e-24));
}

unsigned e0PoolWidth() {
    if (const std::optional<std::string> env = VoragoTest::sweepEnv("VORAGO_SWEEP_THREADS")) {
        const unsigned long v = std::strtoul(env->c_str(), nullptr, 10);
        if (v > 0ul) {
            return static_cast<unsigned>(v);
        }
    }
    const unsigned hw = std::thread::hardware_concurrency();
    return std::clamp(hw, 1u, 4u);
}

}  // namespace

TEST_CASE("Vorago_PresetPilot_DefaultSurfaceTakeCurve", "[.probe][vorago]") {
    constexpr int kSeeds = VoragoTest::kNumSeedIndices;
    const unsigned width = e0PoolWidth();

    std::printf("\n=== Vorago_PresetPilot_DefaultSurfaceTakeCurve (E0, FR-017a) ===\n");
    std::printf("default surface, %d seed indices, %.0f s each, 48 kHz, block 512, pool width %u\n",
                kSeeds, kE0EndSeconds, width);
    std::printf("timeline: A = %.0f s  M1 = [%.0f, %.0f]  M2 = [%.0f, %.0f]  M3 = [%.0f, %.0f]  "
                "H = %.0f s\n",
                kE0AttackSeconds, kE0Minutes[0].first, kE0Minutes[0].second, kE0Minutes[1].first,
                kE0Minutes[1].second, kE0Minutes[2].first, kE0Minutes[2].second, kE0EndSeconds);
    std::fflush(stdout);

    std::vector<E0SeedResult> results(static_cast<std::size_t>(kSeeds));
    Krate::DSP::TestUtils::RenderFingerprint pooledSeed0L{};
    Krate::DSP::TestUtils::RenderFingerprint pooledSeed0R{};

    // ---- Pooled renders, in batches of the pool width ----------------------------
    for (int first = 0; first < kSeeds; first += static_cast<int>(width)) {
        const int last = std::min(kSeeds, first + static_cast<int>(width));
        std::printf("[e0] rendering seeds %d..%d (340 s each)...\n", first, last - 1);
        std::fflush(stdout);

        std::vector<VoragoTest::SweepCapture> caps(static_cast<std::size_t>(last - first));
        std::vector<std::function<void()>> jobs;
        jobs.reserve(caps.size());
        for (int i = first; i < last; ++i) {
            VoragoTest::SweepCapture* slot = &caps[static_cast<std::size_t>(i - first)];
            jobs.emplace_back([slot, i] { *slot = VoragoTest::renderPreset(e0Spec(i)); });
        }
        VoragoTest::runJobs(jobs, width);

        // Description on the test thread (non-thread-safe metric statics).
        for (int i = first; i < last; ++i) {
            VoragoTest::SweepCapture& c = caps[static_cast<std::size_t>(i - first)];
            E0SeedResult& r = results[static_cast<std::size_t>(i)];
            r.finite = c.finite;
            r.peak = c.peak;
            if (c.capL.size() == kE0Minutes.size() && c.capR.size() == kE0Minutes.size()) {
                for (std::size_t m = 0; m < kE0Minutes.size(); ++m) {
                    r.minutes[m] = VoragoTest::describe(c.capL[m], c.capR[m], kE0SampleRate);
                }
                r.m1RmsDb = e0StereoRmsDb(c.capL[0], c.capR[0]);
                if (i == 0) {
                    pooledSeed0L = Krate::DSP::TestUtils::fingerprintRender(c.capL[0]);
                    pooledSeed0R = Krate::DSP::TestUtils::fingerprintRender(c.capR[0]);
                }
            } else {
                r.finite = false;  // host failure: renderPreset returned early
            }
        }
        // caps (up to width x ~69 MB) is released here.
    }

    // ---- Pool trust (plan 11): seed 0 once more, serially -------------------------
    std::printf("[e0] rendering seed 0 serially (pool trust check)...\n");
    std::fflush(stdout);
    const VoragoTest::SweepCapture serial = VoragoTest::renderPreset(e0Spec(0));

    // ---- Per-seed M1 RMS ------------------------------------------------------------
    std::printf("\n%-6s %-14s %-10s %-6s\n", "seed", "M1 RMS dBFS", "peak", "finite");
    for (int i = 0; i < kSeeds; ++i) {
        const E0SeedResult& r = results[static_cast<std::size_t>(i)];
        std::printf("%-6d %-14.2f %-10.4f %-6s\n", i, r.m1RmsDb, static_cast<double>(r.peak),
                    r.finite ? "yes" : "NO");
    }

    // ---- t_K curve --------------------------------------------------------------------
    std::array<double, kE0Takes.size()> tK{};
    for (std::size_t k = 0; k < kE0Takes.size(); ++k) {
        const int K = kE0Takes[k];
        std::vector<VoragoTest::PresetDescriptor> setA;
        std::vector<VoragoTest::PresetDescriptor> setB;
        setA.reserve(3u * static_cast<std::size_t>(K));
        setB.reserve(3u * static_cast<std::size_t>(K));
        for (int j = 0; j < K; ++j) {
            const E0SeedResult& a =
                results[static_cast<std::size_t>(VoragoTest::takeSeedIndex(0, 0, j, K))];
            const E0SeedResult& b =
                results[static_cast<std::size_t>(VoragoTest::takeSeedIndex(0, 1, j, K))];
            setA.insert(setA.end(), a.minutes.begin(), a.minutes.end());
            setB.insert(setB.end(), b.minutes.begin(), b.minutes.end());
        }
        const VoragoTest::PresetDescriptor dA =
            VoragoTest::meanOf(std::span<const VoragoTest::PresetDescriptor>(setA));
        const VoragoTest::PresetDescriptor dB =
            VoragoTest::meanOf(std::span<const VoragoTest::PresetDescriptor>(setB));
        tK[k] = VoragoTest::descriptorDistance(dA, dB);
    }

    std::printf("\n%-4s %-10s %-10s %s\n", "K", "t_K", "2*t_K", "pass(2*t_K <= 4.0)");
    for (std::size_t k = 0; k < kE0Takes.size(); ++k) {
        const double twice = 2.0 * tK[k];
        std::printf("%-4d %-10.4f %-10.4f %s\n", kE0Takes[k], tK[k], twice,
                    (twice <= VoragoTest::kFloorF) ? "yes" : "no");
    }

    const double t1 = tK[0];
    std::printf("\nt_1 = %.4f  13b t0on = %.4f  |diff| = %.6f  tolerance = %.4f\n", t1, kE0T0On13b,
                std::fabs(t1 - kE0T0On13b), kE0T1Tolerance);

    const double twiceT8 = 2.0 * tK[kE0Takes.size() - 1u];
    std::printf("E0: 2*t_8 = %.4f -> %s\n", twiceT8,
                (twiceT8 <= VoragoTest::kFloorF) ? "PROCEED" : "STOP (FR-017a)");

    // ---- Pool trust comparison ----------------------------------------------------------
    const bool serialShapeOk = serial.capL.size() == kE0Minutes.size() &&
                               serial.capR.size() == kE0Minutes.size();
    Krate::DSP::TestUtils::FingerprintComparison cmpL{};
    Krate::DSP::TestUtils::FingerprintComparison cmpR{};
    if (serialShapeOk) {
        cmpL = Krate::DSP::TestUtils::compareFingerprints(
            Krate::DSP::TestUtils::fingerprintRender(serial.capL[0]), pooledSeed0L);
        cmpR = Krate::DSP::TestUtils::compareFingerprints(
            Krate::DSP::TestUtils::fingerprintRender(serial.capR[0]), pooledSeed0R);
    }
    std::printf("pool trust (seed 0 M1, serial vs pooled): L metric %.3e sample %.3e %s | "
                "R metric %.3e sample %.3e %s\n",
                cmpL.worstMetricRelativeError, static_cast<double>(cmpL.worstSampleError),
                cmpL.withinTolerance() ? "ok" : "MISMATCH", cmpR.worstMetricRelativeError,
                static_cast<double>(cmpR.worstSampleError),
                cmpR.withinTolerance() ? "ok" : "MISMATCH");
    std::fflush(stdout);

    // ---- Assertions (only these) ----------------------------------------------------------
    for (int i = 0; i < kSeeds; ++i) {
        const E0SeedResult& r = results[static_cast<std::size_t>(i)];
        INFO("seed " << i << " peak " << r.peak << " M1 RMS " << r.m1RmsDb << " dBFS");
        REQUIRE(r.finite);
        REQUIRE(r.peak <= kE0OutputCeiling);
        REQUIRE(r.m1RmsDb >= kE0MinM1RmsDb);
    }
    INFO("serial seed-0 render peak " << serial.peak);
    REQUIRE(serial.finite);
    REQUIRE(serial.peak <= kE0OutputCeiling);
    REQUIRE(serialShapeOk);
    INFO("pool trust L: " << cmpL.detail);
    REQUIRE(cmpL.withinTolerance());
    INFO("pool trust R: " << cmpR.detail);
    REQUIRE(cmpR.withinTolerance());
}

// ==============================================================================
// T038 - Vorago_PresetPilot_Calibrate (FR-017a, plan 6.16 steps 2-5; gate G2).
// HIDDEN ([.probe]) and run once by hand, alone (T039). The TU IS the
// measurement; verdicts are printed and read from the log, never asserted.
//
//  - Timeline cross-check: makeTimeline(decode(default getState), false) gives
//    exactly the E0 constants above (A, M1..M3, H) - REQUIREd first, so a
//    mismatch aborts before any render.
//  - P0 = the default surface (no setState); P1..P4 = the four T037 defs by
//    name (the two colony-knob pilots left the set at gate G2, ruling
//    2026-09-29: E6.hi / E7.hi are secondaries, N = 38); each rendered as ordinary main takes (computeTakes) at all 16 seed
//    indices on its own timeline. computeTakes' take j renders at
//    takeSeedIndex(s, 0, j, 16) = (s + j) mod 16, so takes[0, K) is A_K and
//    takes[K, 2K) is B_K for every K <= 8 (plan 6.5, P2-2).
//  - t_K = d(D(A_K), D(B_K)) for K in {1, 2, 4, 8}; rule K = the smallest K
//    with 2 t_K <= 4.0 for EVERY P0..P4 (none: NONE, nothing else scored).
//  - With K: P3' = "Tectonic Floor" with 600 at stored + 1/24 normalized
//    (+2 dB) and 700 at stored + 0.02 (G2 ruling 2026-09-29: the first pair,
//    +6 dB / +0.05, scored 4.81 on a preset whose own s(P) is 4.95), built HERE
//    only (never in allPresets()); d(P3, P3') over A_K (< 4.0 required, Q6).
//    Each pilot primary is scored at the primary bar through its plan 6.7 /
//    6.8 twin at the stored seed with the sweep's own rule
//    (verifiedAt(..., Primary)): bar F = 4.0 (G2 ruling: s(P) and t_1 are
//    recorded, not gated); the Growth attack window also needs d_att >= d_Sus
//    + 1.5; the D1 primary also needs its S10 conjunct at the secondary bar.
//  - Printed: every t_K curve, the ruled K, d(P3, P3'), every s(P), primary d,
//    bar and verdict, the wall clock, and
//    `G2: PROCEED with K = <k> | STOP (<reason>)`.
//
// Assertions (only): the timeline cross-check; every render finite (bit
// pattern) with peak <= 0.9661 (a pilot that does not build counts as a failed
// render).
// ==============================================================================

namespace {

namespace PD = ::Vorago::PresetDefs;

constexpr std::array<std::string_view, 4> kPilotNames{
    "Tectonic Floor",  // P1: S5   (the near-variant host; G2: was P3)
    "Cathedral Void",  // P2: S8
    "Growth Ring",     // P3: D8.2
    "Glass Well",      // P4: D1.Glass
};
constexpr std::size_t kP3Slot = 1u;            // Tectonic Floor = pilots[1] (P0 is pilots[0])
constexpr double kP3PrimeSubStep = 1.0 / 24.0;  // 600 lin [-24, 24] dB: +2 dB (G2 ruling)
constexpr double kP3PrimeSmearStep = 0.02;      // 700: one small section tweak (G2 ruling)
constexpr std::string_view kP3PrimeName = "Tectonic Floor (pilot near-variant P3')";

struct PilotRenderCheck {
    std::string what;
    bool finite = false;
    float peak = 0.0f;
};

struct PilotPreset {
    std::string label;
    const PD::VoragoPresetDef* def = nullptr;  // nullptr: P0, the default surface
    std::vector<std::uint8_t> comp;            // empty: the default surface (no setState)
    bool ready = false;
    std::string why;
    VoragoTest::SweepTimeline tl{};
    int storedSeed = 0;
    std::vector<VoragoTest::TakeRecord> takes;  // 16: take j at seed (s + j) mod 16
    std::array<double, kE0Takes.size()> tK{};
    double selfDistance = 0.0;  // s(P) over A_K (after the K ruling)
};

/// The primary's twin plan (plan 6.7 / 6.8) and, for a StateWithS primary, its
/// S conjunct's twin; indices into the shared render list (-1: not rendered).
struct PilotPrimaryPlan {
    PD::Capability cell = PD::Capability::Count;
    VoragoTest::CellOutcome o;
    int twin = -1;
    bool attack = false;
    double wEnd = 0.0;
    double susRev0 = 0.0;  // Sus_rev = [A_rev + 5, A_rev + 65] (attack window only)
    double susRev1 = 0.0;
    PD::Capability conjCell = PD::Capability::Count;
    VoragoTest::CellOutcome conj;
    int conjTwin = -1;
};

const PD::VoragoPresetDef* findPilotDef(std::string_view name) {
    for (const PD::VoragoPresetDef& d : PD::allPresets()) {
        if (d.name == name) {
            return &d;
        }
    }
    return nullptr;
}

/// makeTimeline(decode(comp, or the default getState when empty), false) and
/// the stored seed index.
bool pilotDecodeTimeline(std::span<const std::uint8_t> comp, VoragoTest::SweepTimeline& tl,
                         int& storedSeed) {
    std::vector<std::uint8_t> scratch;
    VoragoTest::DecodedPresetState st;
    if (!VoragoTest::decodePresetState(VoragoTest::detail::stateBytesOrDefault(comp, scratch),
                                       st)) {
        return false;
    }
    tl = VoragoTest::makeTimeline(st, false);
    storedSeed = st.global.seedIndex.load(std::memory_order_relaxed);
    return true;
}

std::vector<VoragoTest::TakeRecord> takeSlice(const std::vector<VoragoTest::TakeRecord>& takes,
                                              int first, int count) {
    const auto b = takes.begin() + static_cast<std::ptrdiff_t>(first);
    return {b, b + static_cast<std::ptrdiff_t>(count)};
}

void setPoint(std::vector<PD::ParamSetting>& params, Steinberg::Vst::ParamID id, double v) {
    for (PD::ParamSetting& p : params) {
        if (p.id == id) {
            p.normalized = v;
            return;
        }
    }
    params.push_back(PD::ParamSetting{.id = id, .normalized = v});
}

/// P3': Tectonic Floor's definition with 600 at its stored normalized + 1/24
/// (+2 dB) and 700 at its stored normalized + 0.02 (G2 ruling 2026-09-29), built
/// through the shipped drive. Local to this TU.
bool buildP3Prime(const PilotPreset& p3, std::vector<std::uint8_t>& comp, std::string& why) {
    const std::span<const std::uint8_t> bytes(p3.comp);
    const std::optional<double> sub =
        VoragoTest::detail::storedNormalized(bytes, ::Vorago::kSubLevelOffsetId);
    const std::optional<double> smear =
        VoragoTest::detail::storedNormalized(bytes, ::Vorago::kSmearAmountId);
    if (p3.def == nullptr || !sub.has_value() || !smear.has_value()) {
        why = "P3's stored 600 / 700 are not readable";
        return false;
    }
    PD::VoragoPresetDef variant = *p3.def;
    variant.name = kP3PrimeName;
    setPoint(variant.params, ::Vorago::kSubLevelOffsetId, *sub + kP3PrimeSubStep);
    setPoint(variant.params, ::Vorago::kSmearAmountId, *smear + kP3PrimeSmearStep);
    return VoragoTest::buildPresetComponentState(variant, comp, why);
}

/// Plans the primary's twin exactly as computeVerificationVector's pass 1 does
/// for that one cell (and its S conjunct), appending the RenderSpecs to `specs`.
PilotPrimaryPlan planPrimary(const PilotPreset& p, std::vector<VoragoTest::RenderSpec>& specs) {
    using C = PD::Capability;
    using V = PD::Verification;
    PilotPrimaryPlan plan;
    plan.cell = p.def->primary;
    const std::span<const std::uint8_t> comp(p.comp);

    VoragoTest::DecodedPresetState st;
    if (!VoragoTest::decodePresetState(comp, st)) {
        VoragoTest::markSkipped(plan.o, std::string(VoragoTest::kSkipDecodeFailed));
        return plan;
    }
    const std::map<Steinberg::Vst::ParamID, double> stored =
        VoragoTest::storedNormalizedValues(comp);
    const double twoS = 2.0 * p.selfDistance;

    const auto planTwin = [&](C c, VoragoTest::CellOutcome& o) -> int {
        o.stateOk = VoragoTest::statePredicate(c, st);
        o.conjunctOk = true;
        o.twoS = twoS;
        o.attribBase = -1.0;
        const V kind = PD::cellSpecs()[static_cast<std::size_t>(c)].verification;
        if (kind == V::RouteIsolated || kind == V::FreezeGesture || kind == V::StateOnly) {
            VoragoTest::markSkipped(o, "not a pilot primary kind");
            return -1;
        }
        const VoragoTest::ParamOverrides ov = VoragoTest::twinOverrides(c, st);
        const std::string why = VoragoTest::skipReason(c, st, stored, ov);
        if (!why.empty()) {
            VoragoTest::markSkipped(o, why);
            return -1;
        }
        if (kind == V::AttackWindow) {
            // Plan 6.8: P_rev to max(W_end, A_rev + 65), capturing [0, W_end] and
            // Sus_rev; its own H lies past the end (no release).
            const std::optional<double> wEnd = VoragoTest::attackWindowEndSeconds(p.def, comp);
            const std::optional<double> aRev = VoragoTest::revertedAttackSpanSeconds(c, comp);
            if (p.takes.empty() || p.takes.front().attackCapL.empty() || !wEnd.has_value() ||
                !aRev.has_value()) {
                VoragoTest::markSkipped(o, std::string(VoragoTest::kSkipNoAttackCapture));
                return -1;
            }
            VoragoTest::RenderSpec spec;
            spec.comp = comp;
            spec.block0 = ov;
            spec.seedIndex = p.storedSeed;
            spec.sr = VoragoTest::kSweepSampleRate;
            spec.noteOffAt = *aRev + 185.0;
            spec.end = std::max(*wEnd, *aRev + 185.0);
            spec.capture = {{0.0, *wEnd},
                            {*aRev + 5.0, *aRev + 65.0},
                            {*aRev + 65.0, *aRev + 125.0},
                            {*aRev + 125.0, *aRev + 185.0}};  // ruling 2026-09-30: M1..M3
            plan.attack = true;
            plan.wEnd = *wEnd;
            plan.susRev0 = *aRev + 5.0;
            plan.susRev1 = *aRev + 65.0;
            specs.push_back(std::move(spec));
            return static_cast<int>(specs.size()) - 1;
        }
        specs.push_back(VoragoTest::detail::twinSusSpec(comp, p.tl, p.storedSeed, ov));
        return static_cast<int>(specs.size()) - 1;
    };

    plan.twin = planTwin(plan.cell, plan.o);
    const C sc = PD::cellSpecs()[static_cast<std::size_t>(plan.cell)].sConjunct;
    if (sc != C::Count) {
        plan.conjCell = sc;
        plan.conjTwin = planTwin(sc, plan.conj);
    }
    return plan;
}

/// D3 primary rule (ruling 2026-09-30), as computeVerificationVector's pass 3
/// applies it: a noise-model primary carries its S1 conjunct's render terms, so
/// verifiedAt(o, D3.x, Primary) reads stateOk && conjunctOk && rendered && d >= F.
/// Call after the conjunct is scored. No-op for every other primary.
void applyD3PrimaryRule(PilotPrimaryPlan& plan) {
    using C = PD::Capability;
    if (!VoragoTest::detail::capInRange(plan.cell, C::D3Direct, C::D3MetallicHiss)) {
        return;
    }
    plan.o.rendered = plan.conj.rendered;
    plan.o.d = plan.conj.d;
    plan.o.attribBase = -1.0;
    plan.o.skip = plan.conj.rendered ? std::string() : plan.conj.skip;
}

/// Pass 2 for one twin, as computeVerificationVector scores it: d =
/// d(P_Sus, describe(twin Sus)); or, for the attack window, d = d_att and
/// attribBase = d_Sus (plan 6.8), with the time-to-level lines printed.
void scoreTwin(const PilotPreset& p, const PilotPrimaryPlan& plan, int index, bool attack,
               const std::vector<VoragoTest::SweepCapture>& caps, VoragoTest::CellOutcome& o) {
    constexpr double kSr = VoragoTest::kSweepSampleRate;
    if (index < 0) {
        return;  // not rendered; the plan-time reason is already recorded
    }
    const VoragoTest::SweepCapture& cap = caps[static_cast<std::size_t>(index)];
    const VoragoTest::TakeRecord& storedTake = p.takes.front();
    // Ruling 2026-09-30: P_Sus and the twin are both the mean of three minutes.
    const VoragoTest::PresetDescriptor pSus = VoragoTest::meanOf(
        std::span<const VoragoTest::PresetDescriptor>(storedTake.minutes));

    if (!attack) {
        const std::optional<VoragoTest::PresetDescriptor> twin =
            VoragoTest::detail::twinSusDescriptor(cap, 0u, kSr);
        if (!twin.has_value()) {
            VoragoTest::markSkipped(o, std::string(VoragoTest::kSkipRenderFailed));
            return;
        }
        o.rendered = true;
        o.d = VoragoTest::descriptorDistance(pSus, *twin);
        return;
    }

    const std::optional<VoragoTest::PresetDescriptor> susRev =
        VoragoTest::detail::twinSusDescriptor(cap, 1u, kSr);
    if (!cap.finite || cap.capL.size() < 4u || cap.capL[0].empty() || !susRev.has_value() ||
        storedTake.attackCapL.empty()) {
        VoragoTest::markSkipped(o, std::string(VoragoTest::kSkipRenderFailed));
        return;
    }
    const double susRevDb = VoragoTest::detail::spanDb(cap, plan.susRev0, plan.susRev1, kSr);
    const std::optional<double> reachRev = VoragoTest::detail::firstSecondAtOrAbove(
        cap.capL[0], cap.capR[0], kSr, susRevDb - VoragoTest::kAttackReachBelowSusDb);
    // FR-016 (b), plan 3.3: the comparison window is the measured reach + 5 s, inside
    // the registered-bound [0, W_end] capture.
    const std::optional<double> wMeas = VoragoTest::measuredAttackWindowEndSeconds(
        storedTake.attackReachSeconds, reachRev, plan.wEnd);
    if (!wMeas.has_value()) {
        VoragoTest::markSkipped(o, std::string(VoragoTest::kSkipReachOutsideCapture));
        std::printf("  attack window: W_end registered %.1f s, measured none (reach outside "
                    "capture)\n",
                    plan.wEnd);
    } else {
        const double floorDb = storedTake.susDb - VoragoTest::kAttackFloorBelowSusDb;
        const VoragoTest::PresetDescriptor attP = VoragoTest::detail::measuredAttackDescriptor(
            storedTake.attackCapL, storedTake.attackCapR, *wMeas, kSr, floorDb);
        const VoragoTest::PresetDescriptor attRev = VoragoTest::detail::measuredAttackDescriptor(
            cap.capL[0], cap.capR[0], *wMeas, kSr, floorDb);
        o.rendered = true;
        o.d = VoragoTest::descriptorDistance(attP, attRev);
        o.attribBase = VoragoTest::descriptorDistance(pSus, *susRev);
        std::printf("  attack window (W_end registered %.1f s, measured %.1f s): d_att %.4f, "
                    "d_Sus %.4f (attributable iff d_att >= %.4f)\n",
                    plan.wEnd, *wMeas, o.d, o.attribBase,
                    o.attribBase + VoragoTest::kAttribMargin);
    }
    VoragoTest::detail::printReach("P", storedTake.attackReachSeconds);
    VoragoTest::detail::printReach("P_rev", reachRev);
}

std::string cellLabel(PD::Capability c) {
    if (static_cast<std::size_t>(c) >= PD::kNumCapabilities) {
        return "<none>";
    }
    return std::string(PD::cellSpecs()[static_cast<std::size_t>(c)].label);
}

}  // namespace

TEST_CASE("Vorago_PresetPilot_Calibrate", "[.probe][vorago]") {
    using C = PD::Capability;
    constexpr int kAllSeeds = VoragoTest::kNumSeedIndices;
    const auto wallStart = std::chrono::steady_clock::now();
    const unsigned width = e0PoolWidth();

    std::printf("\n=== Vorago_PresetPilot_Calibrate (FR-017a, plan 6.16 steps 2-5, G2) ===\n");
    std::printf("P0 default surface + P1..P4 (T037, G2 set), %d seed indices each, 48 kHz, block 512, "
                "pool width %u\n",
                kAllSeeds, width);
    std::fflush(stdout);

    // ---- Timeline cross-check (plan 6.16 step 1 constants vs makeTimeline) --------------
    std::vector<std::uint8_t> defaultBytes;
    REQUIRE(VoragoTest::defaultSurfaceState(defaultBytes));
    VoragoTest::SweepTimeline defaultTl{};
    int defaultSeed = 0;
    REQUIRE(pilotDecodeTimeline(std::span<const std::uint8_t>(defaultBytes), defaultTl,
                                defaultSeed));
    std::printf("timeline cross-check: makeTimeline(default) A = %.3f  M1 = [%.3f, %.3f]  "
                "M2 = [%.3f, %.3f]  M3 = [%.3f, %.3f]  H = %.3f  (E0: %.0f / %.0f)\n",
                defaultTl.A, defaultTl.m[0][0], defaultTl.m[0][1], defaultTl.m[1][0],
                defaultTl.m[1][1], defaultTl.m[2][0], defaultTl.m[2][1], defaultTl.H,
                kE0AttackSeconds, kE0EndSeconds);
    std::fflush(stdout);
    REQUIRE(defaultTl.A == kE0AttackSeconds);
    for (std::size_t k = 0; k < kE0Minutes.size(); ++k) {
        INFO("minute M" << (k + 1u));
        REQUIRE(defaultTl.m[k][0] == kE0Minutes[k].first);
        REQUIRE(defaultTl.m[k][1] == kE0Minutes[k].second);
    }
    REQUIRE(defaultTl.H == kE0EndSeconds);

    // ---- The pilot set -----------------------------------------------------------------
    std::vector<PilotPreset> pilots(1u + kPilotNames.size());
    pilots[0].label = "P0 " + std::string(VoragoTest::kDefaultSurfaceName);
    pilots[0].tl = defaultTl;
    pilots[0].storedSeed = defaultSeed;
    pilots[0].ready = true;  // comp stays empty: the true default surface
    for (std::size_t i = 0; i < kPilotNames.size(); ++i) {
        PilotPreset& p = pilots[i + 1u];
        p.label = "P" + std::to_string(i + 1u) + " " + std::string(kPilotNames[i]);
        p.def = findPilotDef(kPilotNames[i]);
        if (p.def == nullptr) {
            p.why = "not in allPresets() (T037)";
            continue;
        }
        if (!VoragoTest::buildPresetComponentState(*p.def, p.comp, p.why)) {
            continue;
        }
        if (!pilotDecodeTimeline(std::span<const std::uint8_t>(p.comp), p.tl, p.storedSeed)) {
            p.why = "decode failed";
            continue;
        }
        p.ready = true;
    }

    std::vector<PilotRenderCheck> checks;

    // ---- Step 2: every pilot at all 16 seed indices, t_K curves -------------------------
    for (PilotPreset& p : pilots) {
        if (!p.ready) {
            std::printf("[pilot] %s: NOT RENDERED (%s)\n", p.label.c_str(), p.why.c_str());
            checks.push_back(PilotRenderCheck{
                .what = p.label + " not rendered: " + p.why, .finite = false, .peak = 0.0f});
            continue;
        }
        std::printf("[pilot] %s: stored seed %d, A = %.1f s, H = %.1f s, total = %.1f s, "
                    "rendering %d takes...\n",
                    p.label.c_str(), p.storedSeed, p.tl.A, p.tl.H, p.tl.total, kAllSeeds);
        std::fflush(stdout);
        p.takes = VoragoTest::computeTakes(
            p.comp, p.tl, p.storedSeed, kAllSeeds, width,
            VoragoTest::attackWindowEndSeconds(p.def, std::span<const std::uint8_t>(p.comp)));
        for (const VoragoTest::TakeRecord& t : p.takes) {
            checks.push_back(PilotRenderCheck{
                .what = p.label + " take at seed " + std::to_string(t.seedIndex),
                .finite = t.finite,
                .peak = t.peak});
        }
        for (std::size_t k = 0; k < kE0Takes.size(); ++k) {
            const int K = kE0Takes[k];
            p.tK[k] = VoragoTest::descriptorDistance(
                VoragoTest::takesMean(takeSlice(p.takes, 0, K)),
                VoragoTest::takesMean(takeSlice(p.takes, K, K)));
        }
    }

    std::printf("\n%-34s", "t_K curve (2*t_K <= 4.0 ?)");
    for (const int K : kE0Takes) {
        std::printf(" K=%-13d", K);
    }
    std::printf("\n");
    for (const PilotPreset& p : pilots) {
        std::printf("%-34s", p.label.c_str());
        for (std::size_t k = 0; k < kE0Takes.size(); ++k) {
            if (!p.ready) {
                std::printf(" %-15s", "n/a");
                continue;
            }
            std::printf(" %-8.4f %-6s", p.tK[k],
                        (2.0 * p.tK[k] <= VoragoTest::kFloorF) ? "yes" : "no");
        }
        std::printf("\n");
    }

    // ---- Step 3: rule K --------------------------------------------------------------
    int ruledK = 0;
    for (std::size_t k = 0; k < kE0Takes.size() && ruledK == 0; ++k) {
        const bool all = std::ranges::all_of(pilots, [k](const PilotPreset& p) {
            return p.ready && 2.0 * p.tK[k] <= VoragoTest::kFloorF;
        });
        if (all) {
            ruledK = kE0Takes[k];
        }
    }
    if (ruledK > 0) {
        std::printf("\nruled K = %d\n", ruledK);
    } else {
        std::printf("\nruled K = NONE\n");
    }
    std::fflush(stdout);

    std::vector<std::string> stops;
    for (const PilotPreset& p : pilots) {
        if (!p.ready) {
            stops.push_back(p.label + " not rendered (" + p.why + ")");
        }
    }

    // ---- Step 4: with K fixed --------------------------------------------------------
    if (ruledK == 0) {
        stops.emplace_back("no K <= 8 gives 2*t_K <= 4.0 for every P0-P6 (FR-017a)");
    } else {
        for (PilotPreset& p : pilots) {
            if (p.ready) {
                p.selfDistance = VoragoTest::takesSelfDistance(takeSlice(p.takes, 0, ruledK));
            }
        }

        // d(P3, P3') over A_K (Q6).
        const PilotPreset& p3 = pilots[kP3Slot];
        std::vector<std::uint8_t> primeComp;
        std::string primeWhy;
        VoragoTest::SweepTimeline primeTl{};
        int primeSeed = 0;
        std::optional<double> dPair;
        if (p3.ready && buildP3Prime(p3, primeComp, primeWhy) &&
            pilotDecodeTimeline(std::span<const std::uint8_t>(primeComp), primeTl, primeSeed)) {
            std::printf("[pilot] P3' (600 +%.3f, 700 +%.2f normalized): stored seed %d, "
                        "rendering %d takes...\n",
                        kP3PrimeSubStep, kP3PrimeSmearStep, primeSeed, ruledK);
            std::fflush(stdout);
            const std::vector<VoragoTest::TakeRecord> primeTakes =
                VoragoTest::computeTakes(primeComp, primeTl, primeSeed, ruledK, width);
            for (const VoragoTest::TakeRecord& t : primeTakes) {
                checks.push_back(PilotRenderCheck{
                    .what = "P3' take at seed " + std::to_string(t.seedIndex),
                    .finite = t.finite,
                    .peak = t.peak});
            }
            dPair = VoragoTest::descriptorDistance(
                VoragoTest::takesMean(takeSlice(p3.takes, 0, ruledK)),
                VoragoTest::takesMean(primeTakes));
        } else {
            if (primeWhy.empty()) {
                primeWhy = p3.ready ? "P3' decode failed" : "P3 not rendered";
            }
            checks.push_back(PilotRenderCheck{
                .what = "P3' not rendered: " + primeWhy, .finite = false, .peak = 0.0f});
        }
        if (dPair.has_value()) {
            const bool ok = *dPair < VoragoTest::kFloorF;
            std::printf("\nd(P3, P3') over A_%d = %.4f -> %s\n", ruledK, *dPair,
                        ok ? "ok" : "STOP (Q6)");
            if (!ok) {
                stops.emplace_back("d(P3, P3') >= 4.0 (Q6)");
            }
        } else {
            std::printf("\nd(P3, P3') = n/a (%s) -> STOP (Q6)\n", primeWhy.c_str());
            stops.push_back("d(P3, P3') not measured: " + primeWhy);
        }

        // Primary twins of P1..P6, one batch.
        std::vector<VoragoTest::RenderSpec> specs;
        std::vector<PilotPrimaryPlan> plans(pilots.size());
        for (std::size_t i = 1; i < pilots.size(); ++i) {
            if (pilots[i].ready) {
                plans[i] = planPrimary(pilots[i], specs);
            }
        }
        std::printf("[pilot] rendering %zu primary twin(s)...\n", specs.size());
        std::fflush(stdout);
        std::vector<VoragoTest::SweepCapture> caps(specs.size());
        std::vector<std::function<void()>> jobs;
        jobs.reserve(specs.size());
        for (std::size_t j = 0; j < specs.size(); ++j) {
            jobs.emplace_back([&specs, &caps, j] { caps[j] = VoragoTest::renderPreset(specs[j]); });
        }
        VoragoTest::runJobs(jobs, width);
        for (std::size_t j = 0; j < caps.size(); ++j) {
            checks.push_back(PilotRenderCheck{.what = "primary twin render " + std::to_string(j),
                                              .finite = caps[j].finite,
                                              .peak = caps[j].peak});
        }

        std::printf("\n%-34s %-8s\n", "preset", "s(P)");
        std::printf("%-34s %-8.4f (no primary)\n", pilots[0].label.c_str(),
                    pilots[0].selfDistance);
        for (std::size_t i = 1; i < pilots.size(); ++i) {
            const PilotPreset& p = pilots[i];
            if (!p.ready) {
                continue;
            }
            PilotPrimaryPlan& plan = plans[i];
            const std::string label = cellLabel(plan.cell);
            std::printf("%s - primary %s\n", p.label.c_str(), label.c_str());
            scoreTwin(p, plan, plan.twin, plan.attack, caps, plan.o);
            if (plan.conjCell != C::Count) {
                scoreTwin(p, plan, plan.conjTwin, false, caps, plan.conj);
                plan.o.conjunctOk = VoragoTest::verifiedAt(plan.conj, PD::Verification::Ablation,
                                                           VoragoTest::ClaimRole::Secondary);
                applyD3PrimaryRule(plan);
                const std::string conjLabel = cellLabel(plan.conjCell);
                std::printf("  conjunct %s: d %.4f, bar max(%.1f, %.4f) -> %s%s%s\n",
                            conjLabel.c_str(), plan.conj.d, VoragoTest::kSecondaryBar,
                            plan.conj.twoS, plan.o.conjunctOk ? "ok" : "FAIL",
                            plan.conj.skip.empty() ? "" : " skip: ",
                            plan.conj.skip.c_str());
            }
            const bool verified =
                VoragoTest::verifiedAt(plan.o, plan.cell, VoragoTest::ClaimRole::Primary);
            const double bar = VoragoTest::kFloorF;  // G2 ruling: s(P) recorded, not gated
            std::printf("  s(P) %.4f  primary d %.4f  bar %.4f  state %s  -> %s%s%s\n",
                        p.selfDistance, plan.o.d, bar, plan.o.stateOk ? "ok" : "false",
                        verified ? "PASS" : "FAIL", plan.o.skip.empty() ? "" : "  skip: ",
                        plan.o.skip.c_str());
            if (!verified) {
                const bool colonyKnob = (plan.cell == C::E6SyncRateHi ||
                                         plan.cell == C::E7SelfAffinityHi);
                stops.push_back(p.label + " primary " + label + " below its bar" +
                                (colonyKnob ? " (Q5, FR-017)"
                                            : " (C-2.2: re-author and re-run the pilot)"));
            }
        }
    }

    const double wallSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - wallStart).count();
    std::printf("\nwall clock: %.1f s (%.1f min)\n", wallSeconds, wallSeconds / 60.0);
    if (stops.empty()) {
        std::printf("G2: PROCEED with K = %d\n", ruledK);
    } else {
        std::string reason;
        for (const std::string& s : stops) {
            reason += reason.empty() ? s : "; " + s;
        }
        std::printf("G2: STOP (%s)\n", reason.c_str());
    }
    std::fflush(stdout);

    // ---- Assertions (only these, plus the timeline cross-check above) ---------------------
    for (const PilotRenderCheck& c : checks) {
        INFO(c.what << " finite " << c.finite << " peak " << c.peak);
        REQUIRE(c.finite);
        REQUIRE(c.peak <= kE0OutputCeiling);
    }
}

// Phase 13c T013 (FR-030, SC-007): the 15 roster-primary cells (tasks.md
// "Roster" table; spec "Cell roster"). VORAGO_PILOT_ITERATE=verified27 probes
// requiredPrimaryCells() minus these - the 27 sweep-4-verified primaries.
namespace {

constexpr std::array<PD::Capability, 15> kRosterPrimaries{
    PD::Capability::S4Ecology,     PD::Capability::S6Bloom,          PD::Capability::S9Ghost,
    PD::Capability::M2Age,         PD::Capability::M3Density,        PD::Capability::M4Movement,
    PD::Capability::M5Gravity,     PD::Capability::M9Fog,            PD::Capability::M10Life,
    PD::Capability::M12Mass,       PD::Capability::E1PartialBloom,   PD::Capability::E3NoiseWake,
    PD::Capability::E4FeedbackLoopWake, PD::Capability::E5GhostBursts, PD::Capability::D9FastAttack,
};

/// Phase 13d T019 (FR-010c, plan 4.5): one secondary read on the stored take.
struct PilotSecondaryRow {
    PD::Capability cell = PD::Capability::Count;
    double d = 0.0;
    bool verified = false;  ///< verifiedAt(o, cell, Secondary)
};

/// What one probe run reports back to its caller: the stored take's primary
/// verdict and level arms 1-4 (the verdict and levels lines print it in full).
struct PrimaryProbeResult {
    double d = 0.0;
    std::array<bool, 4> arms{};
    bool verified = false;
    /// Phase 13d T019: every secondary the secondary path read (empty when it is off).
    std::vector<PilotSecondaryRow> secondaries;
};

/// Phase 13d T019 (plan 4.5): what an iterate mode forces on runPrimaryProbe,
/// on top of the env options. Default-constructed: the env options alone, as
/// before. `secondaryCells` set: the secondary path runs on exactly these cells
/// (VORAGO_PILOT_SECONDARY / VORAGO_PILOT_CELLS are not read; an empty list
/// renders no secondary).
struct PilotProbeOptions {
    bool fourTakes = false;  ///< as VORAGO_PILOT_TAKES=4
    bool loopBus = false;    ///< as VORAGO_PILOT_LOOPBUS=1
    std::optional<std::vector<PD::Capability>> secondaryCells;
};

/// Phase 13d T019 (FR-010c (a), FR-025, plan 4.5): each roster cell's gate
/// surface - its host preset and its recorded gate override string ("" when the
/// cell has none). The build edits this table in the same edit that records a
/// ruling in artifacts/rulings.md; the lever table is transcribed from it. After
/// the confirming pass transcribes a companion into its preset, its string is
/// emptied. E6.hi and E7.hi share one surface (FR-015: Colony Pulse at one
/// ruled Life); the roster renders that host once and reads both cells through
/// the secondary path with S7.
struct RosterGate {
    PD::Capability cell;
    std::string_view host;
    std::string_view overrides;
};
constexpr std::array<RosterGate, 8> kRosterGateOverrides{{
    {.cell = PD::Capability::E1PartialBloom, .host = "Bloom Colony", .overrides = ""},              // 1300=<ruled> once ruled
    {.cell = PD::Capability::E4FeedbackLoopWake, .host = "Feeding Loops", .overrides = "800=0.5"},  // FR-014: 1.0x events
    {.cell = PD::Capability::E6SyncRateHi, .host = "Colony Pulse", .overrides = ""},                // 109=<ruled L> once ruled
    {.cell = PD::Capability::E7SelfAffinityHi, .host = "Colony Pulse", .overrides = ""},            // the same L as E6.hi
    {.cell = PD::Capability::M2Age, .host = "Erosion", .overrides = ""},
    {.cell = PD::Capability::M4Movement, .host = "Drifting Strata", .overrides = ""},
    {.cell = PD::Capability::M5Gravity, .host = "Stone Gravity", .overrides = ""},
    {.cell = PD::Capability::M10Life, .host = "Teeming", .overrides = "900=0.15"},  // 13d T046: r3b adopted with the companion (rulings.md 2026-10-08)
}};

/// Phase 13d T019 (FR-010c (a)): the roster cells ruled so far, in FR-010b
/// order; VORAGO_PILOT_ITERATE=roster re-reads exactly these. Grown in the same
/// edit that records the ruling in artifacts/rulings.md. (A vector, not a
/// zero-size array, so the empty set compiles without an unreachable loop body.)
const std::vector<PD::Capability> kRosterRuled{
    PD::Capability::E4FeedbackLoopWake,  // 13d T031: W2 kLoopWakeLaneGain 2.5 at 800=0.5 (rulings.md 2026-10-08)
    PD::Capability::M2Age,               // 13d T037: Age -> CavernDecaySeconds amount -18 (rulings.md 2026-10-08)
    PD::Capability::M10Life,             // 13d T046: Life -> EcosystemDepth amount 0.65 at 900=0.15 (rulings.md 2026-10-08)
};

/// Phase 13d T019 (FR-030b): the frozen sweep-5 records (commit 809d6b5f),
/// record_<allPresets() index>.txt, read relative to the repo root (the probe's
/// working directory, tasks.md "Probe run").
constexpr std::string_view kSweep5RecordDir =
    "specs/vorago-phase13d-engine-ceilings/artifacts/sweep5-records";

/// Phase 13c T067 (plan 2.9, ruling P2): VORAGO_PILOT_LEVER parsed into an
/// engineTweak. `text` is the env string as given ("none" when unset, and then
/// `tweak` is empty and every render is the compiled engine).
struct PilotLever {
    std::string text = "none";
    std::function<void(Krate::DSP::VoragoEngine&)> tweak;
};

/// VORAGO_PILOT_LEVER="childGain=0.7,ghostTapDb=18,ghostDensity=0.6,ecologyWetDb=12"
/// (any subset; plan 2.9). Each key drives the VoragoEngine measurement seam of
/// the same lever (vorago_engine.h setBloomChildGain, setGhostTapMakeupDb,
/// setGhostDensity, setEcologyWetMakeupDb). Phase 13d (FR-012, plan 4.3) adds
/// seven keys: partialBloomGain, partialMutationGain, parentCount,
/// childrenPerEvent, loopWakeBase, loopGainSpan, couplingSpan (setPartialBloomLaneGain,
/// setPartialMutationLaneGain, setBloomParentCount, setBloomChildrenPerEvent,
/// setLoopWakeBase, setLoopGainLeverSpan, setCouplingLeverSpan). The lever also
/// rides on the secondaries' renders (VORAGO_PILOT_SECONDARY=1). An unknown key
/// or an item without '=' fails the run. A lever run is never a gate reading (FR-026 reads the
/// compiled constants), so the verdict line always prints "lever: <string>".
PilotLever readPilotLever() {
    PilotLever lever;
    const std::optional<std::string> env = VoragoTest::sweepEnv("VORAGO_PILOT_LEVER");
    if (!env.has_value()) {
        return lever;
    }
    std::optional<float> childGain;
    std::optional<float> ghostTapDb;
    std::optional<float> ghostDensity;
    std::optional<float> ecologyWetDb;
    std::optional<float> partialBloomGain;
    std::optional<float> partialMutationGain;
    std::optional<float> parentCount;
    std::optional<float> childrenPerEvent;
    std::optional<float> loopWakeBase;
    std::optional<float> loopGainSpan;
    std::optional<float> couplingSpan;
    std::size_t pos = 0;
    while (pos < env->size()) {
        const std::size_t comma = env->find(',', pos);
        const std::string item =
            env->substr(pos, (comma == std::string::npos) ? std::string::npos : comma - pos);
        const std::size_t eq = item.find('=');
        if (eq == std::string::npos) {
            FAIL("VORAGO_PILOT_LEVER item without '=': " << item);
        }
        const std::string key = item.substr(0, eq);
        const auto value = static_cast<float>(std::stod(item.substr(eq + 1)));
        if (key == "childGain") {
            childGain = value;
        } else if (key == "ghostTapDb") {
            ghostTapDb = value;
        } else if (key == "ghostDensity") {
            ghostDensity = value;
        } else if (key == "ecologyWetDb") {
            ecologyWetDb = value;
        } else if (key == "partialBloomGain") {
            partialBloomGain = value;
        } else if (key == "partialMutationGain") {
            partialMutationGain = value;
        } else if (key == "parentCount") {
            parentCount = value;
        } else if (key == "childrenPerEvent") {
            childrenPerEvent = value;
        } else if (key == "loopWakeBase") {
            loopWakeBase = value;
        } else if (key == "loopGainSpan") {
            loopGainSpan = value;
        } else if (key == "couplingSpan") {
            couplingSpan = value;
        } else {
            FAIL("unknown VORAGO_PILOT_LEVER key " << key);
        }
        if (comma == std::string::npos) {
            break;
        }
        pos = comma + 1u;
    }
    lever.text = *env;
    lever.tweak = [childGain, ghostTapDb, ghostDensity, ecologyWetDb, partialBloomGain,
                   partialMutationGain, parentCount, childrenPerEvent, loopWakeBase,
                   loopGainSpan, couplingSpan](Krate::DSP::VoragoEngine& e) {
        if (childGain.has_value()) {
            e.setBloomChildGain(*childGain);
        }
        if (ghostTapDb.has_value()) {
            e.setGhostTapMakeupDb(*ghostTapDb);
        }
        if (ghostDensity.has_value()) {
            e.setGhostDensity(*ghostDensity);
        }
        if (ecologyWetDb.has_value()) {
            e.setEcologyWetMakeupDb(*ecologyWetDb);
        }
        if (partialBloomGain.has_value()) {
            e.setPartialBloomLaneGain(*partialBloomGain);
        }
        if (partialMutationGain.has_value()) {
            e.setPartialMutationLaneGain(*partialMutationGain);
        }
        if (parentCount.has_value()) {
            e.setBloomParentCount(*parentCount);
        }
        if (childrenPerEvent.has_value()) {
            e.setBloomChildrenPerEvent(*childrenPerEvent);
        }
        if (loopWakeBase.has_value()) {
            e.setLoopWakeBase(*loopWakeBase);
        }
        if (loopGainSpan.has_value()) {
            e.setLoopGainLeverSpan(*loopGainSpan);
        }
        if (couplingSpan.has_value()) {
            e.setCouplingLeverSpan(*couplingSpan);
        }
    };
    return lever;
}

/// Phase 13c T067: computeTakes / renderTake (preset_test_support.h:2049-2116)
/// with the lever's engineTweak on every take. A copy, not a call, only because
/// renderTake builds its RenderSpec internally; with no lever the probe calls
/// computeTakes itself, so no gate reading takes this path. `held[j]` is take
/// j's SweepCapture::tweakHeld (jobs never touch Catch2; the caller REQUIREs it).
/// `loopBusMeter` (phase 13d T015, plan 2.1) meters every take, as computeTakes'.
std::vector<VoragoTest::TakeRecord> computeLeverTakes(
    const std::vector<std::uint8_t>& comp, const VoragoTest::SweepTimeline& tl, int storedSeed,
    int K, unsigned threads, std::optional<double> attackWindowEnd,
    const std::function<void(Krate::DSP::VoragoEngine&)>& tweak, std::vector<char>& held,
    bool loopBusMeter) {
    std::vector<VoragoTest::TakeRecord> takes(static_cast<std::size_t>(std::max(K, 0)));
    held.assign(takes.size(), 0);
    std::vector<std::function<void()>> jobs;
    jobs.reserve(takes.size());
    for (int j = 0; j < K; ++j) {
        jobs.emplace_back([&comp, &tl, &takes, &held, &attackWindowEnd, &tweak, storedSeed, j, K,
                           loopBusMeter] {
            const auto jj = static_cast<std::size_t>(j);
            const std::optional<double> window = (j == 0) ? attackWindowEnd : std::nullopt;
            VoragoTest::RenderSpec spec;
            spec.comp = std::span<const std::uint8_t>(comp);
            spec.seedIndex = VoragoTest::takeSeedIndex(storedSeed, 0, j, K);
            spec.sr = VoragoTest::kSweepSampleRate;
            spec.noteOffAt = tl.H;
            spec.end = tl.total;
            spec.capture.reserve(4u);
            for (const auto& w : tl.m) {
                spec.capture.emplace_back(w[0], w[1]);
            }
            if (window.has_value()) {
                spec.capture.emplace_back(0.0, *window);
            }
            spec.engineTweak = tweak;
            spec.loopBusMeter = loopBusMeter;

            const VoragoTest::SweepCapture cap = VoragoTest::renderPreset(spec);
            held[jj] = cap.tweakHeld ? 1 : 0;
            const VoragoTest::ArmResult arms = VoragoTest::evaluateArms(cap, tl, spec.sr);

            VoragoTest::TakeRecord& t = takes[jj];
            t.seedIndex = spec.seedIndex;
            t.finite = arms.finite;
            t.peak = arms.peak;
            t.worstHiDb = arms.worstHiDb;
            t.worstLoDb = arms.worstLoDb;
            t.lateVsSusDb = arms.lateVsSusDb;
            t.tailDb = arms.tailDb;
            t.armPass = {arms.pass1, arms.pass2, arms.pass3, arms.pass4};
            for (std::size_t k = 0; k < t.minutes.size(); ++k) {
                if (k < cap.capL.size() && !cap.capL[k].empty()) {
                    t.minutes[k] = VoragoTest::describe(cap.capL[k], cap.capR[k], spec.sr);
                }
            }
            const std::size_t minuteCaps =
                std::min<std::size_t>(t.minutes.size(), cap.capL.size());
            t.levelTwinD = VoragoTest::levelTwinD(
                std::span<const std::vector<float>>(cap.capL.data(), minuteCaps),
                std::span<const std::vector<float>>(cap.capR.data(), minuteCaps), spec.sr);
            t.susDb = arms.susDb;
            if (window.has_value() && cap.capL.size() > 3u && !cap.capL[3].empty()) {
                t.attackReachSeconds = VoragoTest::detail::firstSecondAtOrAbove(
                    cap.capL[3], cap.capR[3], spec.sr,
                    arms.susDb - VoragoTest::kAttackReachBelowSusDb);
                t.attackCapL = cap.capL[3];
                t.attackCapR = cap.capR[3];
            }
            if (loopBusMeter) {
                VoragoTest::fillLoopBusFigures(t, cap, tl, spec.sr);
            }
        });
    }
    VoragoTest::runJobs(jobs, threads);
    return takes;
}

/// The single-preset probe body (Vorago_PresetPilot_PrimaryProbe below), on a
/// def the caller has already patched (or not). Shared by the VORAGO_PILOT_PRESET
/// path and the T013 verified27 iteration; the scoring is unchanged. `lever`
/// (T067) rides on every render this body builds; the verdict line prints it.
PrimaryProbeResult runPrimaryProbe(const PD::VoragoPresetDef& def, const std::string& label,
                                   unsigned width, const PilotLever& lever,
                                   const PilotProbeOptions& opts = PilotProbeOptions{}) {
    using C = PD::Capability;
    std::printf("[probe] lever: %s\n", lever.text.c_str());
    PilotPreset p;
    p.label = label;
    p.def = &def;
    REQUIRE(VoragoTest::buildPresetComponentState(*p.def, p.comp, p.why));
    REQUIRE(pilotDecodeTimeline(std::span<const std::uint8_t>(p.comp), p.tl, p.storedSeed));
    p.ready = true;
    // Phase 13d T018 (FR-019, plan 2.4): VORAGO_PILOT_READBACK=1 renders block 0
    // of the stored take at Life (109) in {stored, 0.0, 1.0} - the stored value
    // with no override, the other two as a block-0 override - and prints voice 0's
    // three Life-row destinations after that block next to the UNCLAMPED row sum.
    //
    // rowsum = the stored plain MB base + sum over VoragoMacroMatrix::kRows on that
    // target of contributionOf(row) (vorago_macro_matrix.h:1167-1176: amount *
    // applyModCurve(curve, m); Gravity bipolar, amount * curve(|g|) * sign(g) with
    // g = (m - 0.5) * 2). Each row's m is the stored macro (ID 100 + index,
    // plugin_ids.h:101; plain == normalized, macro_params.h:114), Life the probed
    // value; channel pressure is 0 in every render (processor.cpp buildMacroVector).
    //
    // The normalized -> plain helper: param_routes.h holds only the route tables
    // (kMbRoutes :199-200, :213 map 800 / 900 / 1301 onto EventRateScale /
    // EcosystemDepth / BloomSpawnRateHz). The conversion the processor applies
    // before pushMacroBases() hands the plain atomic to setTargetBase
    // (processor.cpp:1048-1057) lives in the pack handlers:
    //   800  events_params.h:44-48     Krate::Plugins::logMapFromNormalized(value,
    //                                  kEventsRateScaleMin, kEventsRateScaleMax)
    //   900  ecosystem_params.h:66-69  linearFromNormalized(value, kEcosystemDepthMin,
    //                                  kEcosystemDepthMax)   (param_mapping.h:29)
    //   1301 bloom_params.h:70-74      offsetLogFromNormalized(value, kBloomSpawnRateMinHz,
    //                                  kBloomSpawnRateMaxHz, kBloomSpawnRateEpsHz)
    //                                  (param_mapping.h:44)
    // each cast to float once at the store, as here.
    //
    // Setter clamps (CLAMPED iff dest equals the clamp and rowsum exceeds it):
    // setEcosystemDepth [0, 1] (vorago_voice.h:1536-1541; getEcosystemDepth is the
    // max over the five destinations), setEventRateScale [0.1, 10] (:1601-1606),
    // BloomEngine::setSpawnRateHz [0, kMaxSpawnRateHz] (bloom_engine.h:548-552).
    // Reporting only; the lever rides on these renders as on every other.
    if (VoragoTest::sweepEnv("VORAGO_PILOT_READBACK") == std::optional<std::string>("1")) {
        using Krate::DSP::VoragoMacro;
        using Krate::DSP::VoragoMacroMatrix;
        using Krate::DSP::VoragoMacroTarget;
        const std::map<Steinberg::Vst::ParamID, double> stored =
            VoragoTest::storedNormalizedValues(std::span<const std::uint8_t>(p.comp));
        const auto storedAt = [&stored](Steinberg::Vst::ParamID id) {
            const auto it = stored.find(id);
            REQUIRE(it != stored.end());
            return it->second;
        };
        const double storedLife = storedAt(::Vorago::kMacroLifeId);
        struct ReadbackTarget {
            const char* name;
            VoragoMacroTarget target;
            double base;      // stored plain MB base, as the processor stores it
            double clampHi;   // the destination setter's upper clamp
        };
        const std::array<ReadbackTarget, 3> targets{{
            {.name = "EcosystemDepth", .target = VoragoMacroTarget::EcosystemDepth,
             .base = static_cast<double>(static_cast<float>(::Vorago::linearFromNormalized(
                 storedAt(::Vorago::kEcosystemDepthId), ::Vorago::kEcosystemDepthMin,
                 ::Vorago::kEcosystemDepthMax))),
             .clampHi = 1.0},
            {.name = "EventRateScale", .target = VoragoMacroTarget::EventRateScale,
             .base = static_cast<double>(static_cast<float>(Krate::Plugins::logMapFromNormalized(
                 storedAt(::Vorago::kEventsRateScaleId), ::Vorago::kEventsRateScaleMin,
                 ::Vorago::kEventsRateScaleMax))),
             .clampHi = 10.0},
            {.name = "BloomSpawnRateHz", .target = VoragoMacroTarget::BloomSpawnRateHz,
             .base = static_cast<double>(static_cast<float>(::Vorago::offsetLogFromNormalized(
                 storedAt(::Vorago::kBloomSpawnRateId), ::Vorago::kBloomSpawnRateMinHz,
                 ::Vorago::kBloomSpawnRateMaxHz, ::Vorago::kBloomSpawnRateEpsHz))),
             .clampHi = static_cast<double>(Krate::DSP::BloomEngine::kMaxSpawnRateHz)},
        }};
        const std::array<std::optional<double>, 3> lifeOverride{std::nullopt, 0.0, 1.0};
        for (const std::optional<double>& lifeOv : lifeOverride) {
            const double life = lifeOv.value_or(storedLife);
            std::array<float, 3> dest{};
            bool observedBlock0 = false;
            VoragoTest::RenderSpec spec;
            spec.comp = std::span<const std::uint8_t>(p.comp);
            spec.seedIndex = p.storedSeed;
            spec.sr = VoragoTest::kSweepSampleRate;
            spec.end = static_cast<double>(VoragoTest::detail::kRenderBlock) / spec.sr;
            if (lifeOv.has_value()) {
                spec.block0.emplace_back(::Vorago::kMacroLifeId, *lifeOv);
            }
            spec.engineTweak = lever.tweak;
            spec.blockObserver = [&dest, &observedBlock0](const Krate::DSP::VoragoEngine& e,
                                                          long long start) {
                if (start != 0) {
                    return;
                }
                const Krate::DSP::VoragoVoice& v = e.getVoice(0);
                dest = {v.getEcosystemDepth(), v.getEventRateScale(), v.getBloomSpawnRateHz()};
                observedBlock0 = true;
            };
            const VoragoTest::SweepCapture cap = VoragoTest::renderPreset(spec);
            REQUIRE(cap.finite);
            REQUIRE(cap.tweakHeld);
            REQUIRE(observedBlock0);
            for (std::size_t t = 0; t < targets.size(); ++t) {
                const ReadbackTarget& rt = targets[t];
                double rowsum = rt.base;
                for (const Krate::DSP::VoragoMacroRow& row : VoragoMacroMatrix::kRows) {
                    if (row.target != rt.target) {
                        continue;
                    }
                    const auto macroIndex = static_cast<Steinberg::Vst::ParamID>(row.macro);
                    const double m = (row.macro == VoragoMacro::Life)
                                         ? life
                                         : storedAt(static_cast<Steinberg::Vst::ParamID>(
                                                        ::Vorago::kMacroDarknessId) +
                                                    macroIndex);
                    if (row.macro == VoragoMacro::Gravity) {
                        const double g = (m - 0.5) * 2.0;
                        const double sign = (g < 0.0) ? -1.0 : 1.0;
                        rowsum += static_cast<double>(row.amount) *
                                  static_cast<double>(Krate::DSP::applyModCurve(
                                      row.curve, static_cast<float>(std::fabs(g)))) *
                                  sign;
                    } else {
                        rowsum += static_cast<double>(row.amount) *
                                  static_cast<double>(
                                      Krate::DSP::applyModCurve(row.curve, static_cast<float>(m)));
                    }
                }
                const auto d = static_cast<double>(dest[t]);
                const bool clamped =
                    (dest[t] == static_cast<float>(rt.clampHi)) && rowsum > rt.clampHi;
                std::printf("  readback life %.2f: %s dest %.6f rowsum %.6f [%s]\n", life, rt.name,
                            d, rowsum, clamped ? "CLAMPED" : "ok");
            }
        }
        std::fflush(stdout);
    }
    // Phase 13c T010 (FR-004, E-10): VORAGO_PILOT_TAKES=4 renders all K =
    // kRuledTakes takes of A_K; take 0 is still the stored seed (takeSeedIndex(s,
    // 0, 0, K) == s) and remains the gate. Absent: the stored take alone.
    int takeCount = 1;
    if (const std::optional<std::string> tk = VoragoTest::sweepEnv("VORAGO_PILOT_TAKES")) {
        REQUIRE(*tk == "4");
        takeCount = VoragoTest::kRuledTakes;
    }
    if (opts.fourTakes) {
        takeCount = VoragoTest::kRuledTakes;  // T019: the roster's E4 row
    }
    const std::optional<double> attackWindowEnd =
        VoragoTest::attackWindowEndSeconds(p.def, std::span<const std::uint8_t>(p.comp));
    // Phase 13d T015 (FR-004, FR-021b, plan 2.1): VORAGO_PILOT_LOOPBUS=1 meters
    // every take render (the stored take; all four under VORAGO_PILOT_TAKES=4).
    // Twins and route arms never carry it: it is not part of their scoring.
    const bool loopBus =
        opts.loopBus ||
        VoragoTest::sweepEnv("VORAGO_PILOT_LOOPBUS") == std::optional<std::string>("1");
    if (lever.tweak) {
        std::vector<char> takeHeld;
        p.takes = computeLeverTakes(p.comp, p.tl, p.storedSeed, takeCount, width,
                                    attackWindowEnd, lever.tweak, takeHeld, loopBus);
        for (const char held : takeHeld) {
            REQUIRE(held != 0);
        }
    } else {
        p.takes = VoragoTest::computeTakes(p.comp, p.tl, p.storedSeed, takeCount, width,
                                           attackWindowEnd, loopBus);
    }
    REQUIRE(p.takes.size() == static_cast<std::size_t>(takeCount));
    for (const VoragoTest::TakeRecord& t : p.takes) {
        REQUIRE(t.finite);
        if (loopBus) {
            REQUIRE(t.loopBusMetered);
            REQUIRE(t.loopBusHeld);
        }
    }

    std::vector<VoragoTest::RenderSpec> specs;
    PilotPrimaryPlan plan = planPrimary(p, specs);
    // Route primaries (E1-E5, plan 6.9; sweep-2 re-author loop 2026-09-30): the
    // pilot planner skips RouteIsolated, so the probe adds the four route arms
    // itself - R_k (P + the other four destinations' S overrides), R_k0 (+ depth
    // 0), R_0 and R_00 - and scores d = d(R_k, R_k0), attribBase = d(R_0, R_00)
    // exactly as computeVerificationVector's pass 2 does. planRoute returns the
    // four indices {R_k, R_k0, R_0, R_00} (-1: not a route primary, or skipped);
    // scoreRoute fills plan.o from them. Both serve the stored take and, under
    // VORAGO_PILOT_TAKES=4, every other take (T010).
    const auto planRoute = [](const PilotPreset& pp, PilotPrimaryPlan& pl,
                              std::vector<VoragoTest::RenderSpec>& sp) {
        std::array<int, 4> arms{-1, -1, -1, -1};
        if (PD::cellSpecs()[static_cast<std::size_t>(pl.cell)].verification !=
            PD::Verification::RouteIsolated) {
            return arms;
        }
        VoragoTest::DecodedPresetState st;
        REQUIRE(VoragoTest::decodePresetState(std::span<const std::uint8_t>(pp.comp), st));
        const std::map<Steinberg::Vst::ParamID, double> stored =
            VoragoTest::storedNormalizedValues(std::span<const std::uint8_t>(pp.comp));
        const std::string why = VoragoTest::skipReason(pl.cell, st, stored,
                                                       VoragoTest::routeOverrides(pl.cell));
        if (!why.empty()) {
            VoragoTest::markSkipped(pl.o, why);
            return arms;
        }
        const std::span<const std::uint8_t> comp(pp.comp);
        const auto push = [&](VoragoTest::ParamOverrides ov) {
            sp.push_back(
                VoragoTest::detail::twinSusSpec(comp, pp.tl, pp.storedSeed, std::move(ov)));
            return static_cast<int>(sp.size()) - 1;
        };
        arms[0] = push(VoragoTest::routeOverrides(pl.cell));
        arms[1] = push(VoragoTest::routeOverrides(pl.cell, true));
        arms[2] = push(VoragoTest::routeOverrides(C::Count));
        arms[3] = push(VoragoTest::routeOverrides(C::Count, true));
        pl.o.stateOk = true;
        pl.o.conjunctOk = true;
        pl.o.skip.clear();
        return arms;
    };
    const auto scoreRoute = [](PilotPrimaryPlan& pl, const std::array<int, 4>& arms,
                               const std::vector<VoragoTest::SweepCapture>& cs, bool print) {
        if (arms[0] < 0) {
            return;
        }
        const auto desc = [&](int index) {
            return VoragoTest::detail::twinSusDescriptor(cs[static_cast<std::size_t>(index)], 0u,
                                                         VoragoTest::kSweepSampleRate);
        };
        const auto dK = desc(arms[0]);
        const auto dK0 = desc(arms[1]);
        const auto dNull = desc(arms[2]);
        const auto dNull0 = desc(arms[3]);
        if (!dK || !dK0 || !dNull || !dNull0) {
            VoragoTest::markSkipped(pl.o, std::string(VoragoTest::kSkipRenderFailed));
            return;
        }
        pl.o.rendered = true;
        pl.o.d = VoragoTest::descriptorDistance(*dK, *dK0);
        pl.o.attribBase = VoragoTest::descriptorDistance(*dNull, *dNull0);
        if (print) {
            std::printf("  route arms: d(R_k, R_k0) %.4f, attribBase d(R_0, R_00) %.4f "
                        "(attributable iff d >= %.4f)\n",
                        pl.o.d, pl.o.attribBase, pl.o.attribBase + VoragoTest::kAttribMargin);
        }
    };
    const std::array<int, 4> route = planRoute(p, plan, specs);
    for (VoragoTest::RenderSpec& s : specs) {
        s.engineTweak = lever.tweak;  // T067: twins and route arms
    }
    // Phase 13d T016 (plan 2.2): VORAGO_PILOT_BLOOM=1 observes voice 0's bloom
    // (vorago_voice.h bloom(), bloom_engine.h counters :729-745) on R_k and R_k0
    // of a route primary, the stored take only. Counters are the delta over
    // [A, H] (baseline: after the last block starting before A); liveMean and
    // depthMean are the per-block means of getLiveChildCount() and
    // getSmoothedDepth() over the blocks starting in [A, H). The lambdas write
    // into bloomRead only (jobs run on worker threads, never Catch2).
    struct PilotBloomReadout {
        std::array<std::uint64_t, 4> base{};  // spawn, discarded, spawned, refused
        std::array<std::uint64_t, 4> last{};
        double liveSum = 0.0;
        double depthSum = 0.0;
        std::size_t blocks = 0;
    };
    const bool bloomOn =
        VoragoTest::sweepEnv("VORAGO_PILOT_BLOOM") == std::optional<std::string>("1");
    std::array<PilotBloomReadout, 2> bloomRead{};
    if (bloomOn && route[0] >= 0) {
        const long long aSample = std::llround(p.tl.A * VoragoTest::kSweepSampleRate);
        const long long hSample = std::llround(p.tl.H * VoragoTest::kSweepSampleRate);
        for (std::size_t arm = 0; arm < 2u; ++arm) {
            PilotBloomReadout* read = &bloomRead[arm];
            specs[static_cast<std::size_t>(route[arm])].blockObserver =
                [read, aSample, hSample](const Krate::DSP::VoragoEngine& e, long long start) {
                    const Krate::DSP::BloomEngine& b = e.getVoice(0).bloom();
                    const std::array<std::uint64_t, 4> now{
                        b.getSpawnEventCount(), b.getDiscardedEventCount(),
                        b.getSpawnedChildCount(), b.getRefusedChildCount()};
                    if (start < aSample) {
                        read->base = now;
                        read->last = now;
                        return;
                    }
                    if (start >= hSample) {
                        return;
                    }
                    read->last = now;
                    read->liveSum += static_cast<double>(b.getLiveChildCount());
                    read->depthSum += static_cast<double>(b.getSmoothedDepth());
                    ++read->blocks;
                };
        }
    }
    std::vector<VoragoTest::SweepCapture> caps(specs.size());
    std::vector<std::function<void()>> jobs;
    jobs.reserve(specs.size());
    for (std::size_t j = 0; j < specs.size(); ++j) {
        jobs.emplace_back([&specs, &caps, j] { caps[j] = VoragoTest::renderPreset(specs[j]); });
    }
    VoragoTest::runJobs(jobs, width);
    for (const VoragoTest::SweepCapture& c : caps) {
        REQUIRE(c.finite);
        REQUIRE(c.tweakHeld);
    }

    const std::string primaryLabel = cellLabel(plan.cell);
    std::printf("[probe] %s - primary %s (stored seed %d, %zu twin render(s))\n", p.label.c_str(),
                primaryLabel.c_str(), p.storedSeed, specs.size());
    // The stored take's level arms (plan 6.3), so a candidate that only "passes"
    // by going silent or by sitting on the limiter is caught here, not in the
    // next five-hour sweep (sweep 3 found both: Feedback Mire at -84 dBFS with
    // ecology mix 1.0, Resonant Shaft at the limiter with the cavern at 0.2).
    {
        const VoragoTest::TakeRecord& t = p.takes.front();
        std::printf("  take: peak %.4f  arm1 hi %.2f dB [%s]  arm2 lo %.2f dB [%s]  arm3 late-sus "
                    "%+.2f dB [%s]  arm4 tail %.2f dB [%s]\n",
                    static_cast<double>(t.peak), t.worstHiDb, t.armPass[0] ? "yes" : "NO",
                    t.worstLoDb, t.armPass[1] ? "yes" : "NO", t.lateVsSusDb,
                    t.armPass[2] ? "yes" : "NO", t.tailDb, t.armPass[3] ? "yes" : "NO");
    }
    // Phase 13d T015 (FR-004, FR-021b, plan 2.1): per metered take, the minimum
    // loop-bus 10 s window over [A, H] (arm 2's windows) against kSilenceDb, then
    // voice 0's loops after the take's last block. loopsAlive (verdict line) is
    // the AND over the printed takes; "-" with the meter off.
    const char* loopsAlive = "-";
    if (loopBus) {
        bool alive = true;
        for (std::size_t j = 0; j < p.takes.size(); ++j) {
            const VoragoTest::TakeRecord& t = p.takes[j];
            const bool pass = t.loopBusWindows > 0u && t.loopBusWorstDb >= VoragoTest::kSilenceDb;
            alive = alive && pass;
            std::printf("  loopbus take %zu seed %d: worst10s %.2f dBFS [%s] windows %zu\n", j,
                        t.seedIndex, t.loopBusWorstDb, pass ? "PASS" : "NO", t.loopBusWindows);
            std::printf("  looplife take %zu:", j);
            for (std::size_t l = 0; l < t.loopLifeEnd.size(); ++l) {
                const VoragoTest::LoopLifeReading& r = t.loopLifeEnd[l];
                std::printf("  [%zu] wake %.4f gate %.4f dormant %s gain %.4f", l,
                            static_cast<double>(r.wake), static_cast<double>(r.gate),
                            r.dormant ? "y" : "n", static_cast<double>(r.gain));
            }
            std::printf("\n");
        }
        loopsAlive = alive ? "y" : "n";
    }
    // Phase 13c T011 (FR-004, FR-024b, SC-020): always on, a 44.1 kHz stored-take
    // render of the PATCHED state over [0, A + 65], scored for arm 1 exactly as
    // the sweep's 44.1 kHz arm is (Vorago_PresetSweep_SustainAtAllRates: finite,
    // peak <= kSweepPeakCeiling, every 10 s window <= kRunawayDb). Reporting only.
    bool arm1At441Pass = false;
    {
        VoragoTest::RenderSpec spec441 = VoragoTest::detail::sustainSpec(
            std::span<const std::uint8_t>(p.comp), p.tl, p.storedSeed, VoragoTest::kRate441,
            false);
        spec441.engineTweak = lever.tweak;  // T067
        const VoragoTest::SweepCapture cap441 = VoragoTest::renderPreset(spec441);
        REQUIRE(cap441.tweakHeld);
        bool finite441 = false;
        float peak441 = 0.0f;
        double hi441 = 0.0;
        VoragoTest::detail::scoreRateArm1(cap441,
                                          p.tl.A + VoragoTest::kGestureAfterAttackSeconds,
                                          VoragoTest::kRate441, finite441, peak441, hi441);
        arm1At441Pass = finite441 && peak441 <= VoragoTest::kSweepPeakCeiling &&
                        hi441 <= VoragoTest::kRunawayDb;
        std::printf("  arm1@44.1k: finite %s peak %.4f hi %.2f dB [%s]\n", finite441 ? "y" : "n",
                    static_cast<double>(peak441), hi441, arm1At441Pass ? "PASS" : "NO");
    }
    scoreTwin(p, plan, plan.twin, plan.attack, caps, plan.o);
    scoreRoute(plan, route, caps, true);
    if (bloomOn) {
        if (route[0] < 0) {
            std::printf("  bloom: - (primary %s has no route arms)\n", cellLabel(plan.cell).c_str());
        } else {
            constexpr std::array<const char*, 2> kBloomArm{"R_k", "R_k0"};
            for (std::size_t arm = 0; arm < 2u; ++arm) {
                const PilotBloomReadout& r = bloomRead[arm];
                const auto delta = [&r](std::size_t i) {
                    // A counter below its baseline means the bloom was reset in
                    // [A, H]; the end value is then the count since that reset.
                    return static_cast<unsigned long long>(
                        (r.last[i] >= r.base[i]) ? r.last[i] - r.base[i] : r.last[i]);
                };
                const double n = (r.blocks > 0u) ? static_cast<double>(r.blocks) : 1.0;
                std::printf("  bloom %s: spawn %llu discarded %llu spawned %llu refused %llu "
                            "liveMean %.3f depthMean %.3f\n",
                            kBloomArm[arm], delta(0), delta(1), delta(2), delta(3), r.liveSum / n,
                            r.depthSum / n);
            }
        }
    }
    if (plan.conjCell != C::Count) {
        scoreTwin(p, plan, plan.conjTwin, false, caps, plan.conj);
        plan.o.conjunctOk = VoragoTest::verifiedAt(plan.conj, PD::Verification::Ablation,
                                                   VoragoTest::ClaimRole::Secondary);
        applyD3PrimaryRule(plan);
        std::printf("  conjunct %s: d %.4f, bar %.1f -> %s\n", cellLabel(plan.conjCell).c_str(),
                    plan.conj.d, VoragoTest::kSecondaryBar, plan.o.conjunctOk ? "ok" : "FAIL");
    }
    const bool verified = VoragoTest::verifiedAt(plan.o, plan.cell, VoragoTest::ClaimRole::Primary);
    // T067: "lever: <string>" on the verdict line, so a lever run is never read
    // as a gate figure ("lever: none" is the compiled engine).
    std::printf("  primary d %.4f  bar %.4f  attribBase %.4f  state %s  -> %s%s%s  loopsAlive %s  "
                "lever: %s\n",
                plan.o.d, VoragoTest::kFloorF, plan.o.attribBase, plan.o.stateOk ? "ok" : "false",
                verified ? "PASS" : "FAIL", plan.o.skip.empty() ? "" : "  skip: ",
                plan.o.skip.c_str(), loopsAlive, lever.text.c_str());
    // Phase 13c T011 (FR-024, plan 3.2): one printout a compliance row can cite -
    // the stored take's arms 1-4, then the 44.1 kHz arm-1 render above.
    {
        const VoragoTest::TakeRecord& t = p.takes.front();
        std::printf("  levels: arms [%s %s %s %s] arm1@44.1k [%s]\n", t.armPass[0] ? "y" : "n",
                    t.armPass[1] ? "y" : "n", t.armPass[2] ? "y" : "n", t.armPass[3] ? "y" : "n",
                    arm1At441Pass ? "y" : "n");
    }

    // Phase 13c T009 (FR-004, plan 3.1): VORAGO_PILOT_SECONDARY=1 reads the
    // def's claimed secondaries - plus any cell named in VORAGO_PILOT_CELLS=
    // <label,...> (matched by cellLabel spelling) - from computeVerificationVector
    // on the patched def with the stored take. Reporting only: the primary
    // verdict above is unchanged and remains the gate.
    // Phase 13d T019: opts.secondaryCells, when set, replaces the env selection.
    std::vector<PilotSecondaryRow> secondaryRows;
    if (opts.secondaryCells.has_value() ||
        VoragoTest::sweepEnv("VORAGO_PILOT_SECONDARY") == std::optional<std::string>("1")) {
        std::vector<C> cells(p.def->secondaries.begin(), p.def->secondaries.end());
        if (opts.secondaryCells.has_value()) {
            cells = *opts.secondaryCells;
        } else if (const std::optional<std::string> named =
                       VoragoTest::sweepEnv("VORAGO_PILOT_CELLS")) {
            std::size_t pos = 0;
            while (pos < named->size()) {
                const std::size_t comma = named->find(',', pos);
                const std::string item = named->substr(
                    pos, (comma == std::string::npos) ? std::string::npos : comma - pos);
                C match = C::Count;
                for (std::size_t i = 0; i < PD::kNumCapabilities; ++i) {
                    if (cellLabel(static_cast<C>(i)) == item) {
                        match = static_cast<C>(i);
                        break;
                    }
                }
                if (match == C::Count) {
                    FAIL("unknown cell label " << item);
                }
                if (std::find(cells.begin(), cells.end(), match) == cells.end()) {
                    cells.push_back(match);
                }
                if (comma == std::string::npos) {
                    break;
                }
                pos = comma + 1u;
            }
        }
        if (!cells.empty()) {
            const VoragoTest::TakeRecord& storedTake = p.takes.front();
            const VoragoTest::VerificationVector vec = VoragoTest::computeVerificationVector(
                &def, p.comp, p.tl, 0.0,
                VoragoTest::meanOf(
                    std::span<const VoragoTest::PresetDescriptor>(storedTake.minutes)),
                width, &storedTake, lever.tweak);
            // Phase 13d (plan 4.4): the lever rides on every secondary render too.
            REQUIRE(vec.tweakHeld);
            for (const C c : cells) {
                // 13d T031: a FreezeGesture secondary (D10.1) is scored on the
                // sweep gesture render only (Vorago_PresetSweep_FreezeGesture);
                // computeVerificationVector cannot verify it, so the pilot says
                // so instead of printing a "no" a read set would take as sunk.
                if (PD::cellSpecs()[static_cast<std::size_t>(c)].verification ==
                    PD::Verification::FreezeGesture) {
                    std::printf("  secondary %s: not scored by the pilot (freeze gesture: "
                                "Vorago_PresetSweep_FreezeGesture)\n",
                                cellLabel(c).c_str());
                    continue;
                }
                const VoragoTest::CellOutcome& o = vec.cells[static_cast<std::size_t>(c)];
                const bool secVerified =
                    VoragoTest::verifiedAt(o, c, VoragoTest::ClaimRole::Secondary);
                std::printf("  secondary %s: d %.4f bar %.1f state %s conjunct %s attribBase %.4f "
                            "skip \"%s\" -> %s\n",
                            cellLabel(c).c_str(), o.d, VoragoTest::kSecondaryBar,
                            o.stateOk ? "ok" : "false", o.conjunctOk ? "ok" : "FAIL",
                            o.attribBase, o.skip.c_str(), secVerified ? "VERIFIED" : "no");
                secondaryRows.push_back(PilotSecondaryRow{.cell = c, .d = o.d, .verified = secVerified});
            }
        }
    }

    // Phase 13d T017 (FR-015, plan 2.3): VORAGO_PILOT_LANES=1 renders, on the
    // stored take, P and the ExtReversion twin (twinOverrides: E6.hi 901 -> 0.0,
    // E7.hi 902 -> 0.25, tools/vorago_preset_defs.h:187-190) of each named E6.hi /
    // E7.hi cell (the def's primary and secondaries, plus VORAGO_PILOT_CELLS). A
    // blockObserver samples voice 0's getEcoLaneMean(k) for all five kinds on every
    // block starting in [A, H), into vectors reserved before the render (jobs run
    // on worker threads, never Catch2). Per render and kind it prints mean, p10,
    // p90 and the mean absolute block-to-block change, then one rank line per cell
    // ordering the kinds by |dmean| + |d(p90 - p10)| between P and the twin.
    // Reporting only; the lever rides on these renders as on every other.
    if (VoragoTest::sweepEnv("VORAGO_PILOT_LANES") == std::optional<std::string>("1")) {
        using Kind = Krate::DSP::EcosystemEngine::Kind;
        constexpr std::size_t kKinds = Krate::DSP::EcosystemEngine::kNumKinds;
        constexpr std::array<const char*, kKinds> kKindName{"Partial", "Resonator", "Noise",
                                                            "Feedback", "Ghost"};
        std::vector<C> laneCells;
        const auto addLaneCell = [&laneCells](C c) {
            if ((c == C::E6SyncRateHi || c == C::E7SelfAffinityHi) &&
                std::find(laneCells.begin(), laneCells.end(), c) == laneCells.end()) {
                laneCells.push_back(c);
            }
        };
        addLaneCell(p.def->primary);
        for (const C c : p.def->secondaries) {
            addLaneCell(c);
        }
        if (const std::optional<std::string> named = VoragoTest::sweepEnv("VORAGO_PILOT_CELLS")) {
            for (std::size_t i = 0; i < PD::kNumCapabilities; ++i) {
                const C c = static_cast<C>(i);
                const std::string lbl = cellLabel(c);
                std::size_t pos = 0;
                while (pos < named->size()) {
                    const std::size_t comma = named->find(',', pos);
                    const std::string item = named->substr(
                        pos, (comma == std::string::npos) ? std::string::npos : comma - pos);
                    if (item == lbl) {
                        addLaneCell(c);
                    }
                    if (comma == std::string::npos) {
                        break;
                    }
                    pos = comma + 1u;
                }
            }
        }
        if (laneCells.empty()) {
            std::printf("  lanes: - (no E6.hi / E7.hi cell named)\n");
        } else {
            VoragoTest::DecodedPresetState laneSt;
            REQUIRE(VoragoTest::decodePresetState(std::span<const std::uint8_t>(p.comp), laneSt));
            const std::span<const std::uint8_t> laneComp(p.comp);
            const long long aSample = std::llround(p.tl.A * VoragoTest::kSweepSampleRate);
            const long long hSample = std::llround(p.tl.H * VoragoTest::kSweepSampleRate);
            const long long laneBlock = VoragoTest::detail::kRenderBlock;
            const auto reserveBlocks =
                static_cast<std::size_t>(std::max(0LL, (hSample - aSample) / laneBlock + 2LL));

            // Render 0 is P; render 1 + i is laneCells[i]'s twin.
            const std::size_t laneRenders = 1u + laneCells.size();
            std::vector<std::array<std::vector<float>, kKinds>> traces(laneRenders);
            std::vector<VoragoTest::RenderSpec> laneSpecs;
            laneSpecs.reserve(laneRenders);
            for (std::size_t r = 0; r < laneRenders; ++r) {
                for (std::vector<float>& v : traces[r]) {
                    v.reserve(reserveBlocks);
                }
                VoragoTest::ParamOverrides ov;
                if (r > 0u) {
                    ov = VoragoTest::twinOverrides(laneCells[r - 1u], laneSt);
                    REQUIRE(!ov.empty());
                }
                VoragoTest::RenderSpec spec =
                    VoragoTest::detail::twinSusSpec(laneComp, p.tl, p.storedSeed, std::move(ov));
                spec.engineTweak = lever.tweak;
                std::array<std::vector<float>, kKinds>* trace = &traces[r];
                spec.blockObserver = [trace, aSample, hSample](const Krate::DSP::VoragoEngine& e,
                                                               long long start) {
                    if (start < aSample || start >= hSample) {
                        return;
                    }
                    const Krate::DSP::VoragoVoice& v = e.getVoice(0);
                    for (std::size_t k = 0; k < kKinds; ++k) {
                        (*trace)[k].push_back(v.getEcoLaneMean(static_cast<Kind>(k)));
                    }
                };
                laneSpecs.push_back(std::move(spec));
            }
            std::vector<VoragoTest::SweepCapture> laneCaps(laneRenders);
            std::vector<std::function<void()>> laneJobs;
            laneJobs.reserve(laneRenders);
            for (std::size_t r = 0; r < laneRenders; ++r) {
                laneJobs.emplace_back([&laneSpecs, &laneCaps, r] {
                    laneCaps[r] = VoragoTest::renderPreset(laneSpecs[r]);
                });
            }
            VoragoTest::runJobs(laneJobs, width);
            for (const VoragoTest::SweepCapture& c : laneCaps) {
                REQUIRE(c.finite);
                REQUIRE(c.tweakHeld);
            }

            struct LaneStat {
                double mean = 0.0;
                double p10 = 0.0;
                double p90 = 0.0;
                double dabs = 0.0;
            };
            const auto laneStat = [](const std::vector<float>& x) {
                LaneStat s;
                if (x.empty()) {
                    return s;
                }
                double sum = 0.0;
                double dsum = 0.0;
                for (std::size_t i = 0; i < x.size(); ++i) {
                    sum += static_cast<double>(x[i]);
                    if (i > 0u) {
                        dsum += std::fabs(static_cast<double>(x[i]) - static_cast<double>(x[i - 1u]));
                    }
                }
                s.mean = sum / static_cast<double>(x.size());
                s.dabs = (x.size() > 1u) ? dsum / static_cast<double>(x.size() - 1u) : 0.0;
                std::vector<float> sorted = x;
                std::sort(sorted.begin(), sorted.end());
                const auto at = [&sorted](double q) {
                    const auto idx = static_cast<std::size_t>(
                        std::llround(q * static_cast<double>(sorted.size() - 1u)));
                    return static_cast<double>(sorted[idx]);
                };
                s.p10 = at(0.10);
                s.p90 = at(0.90);
                return s;
            };
            // The short cell tag: the cell label up to its first space ("E6.hi").
            const auto cellTag = [](C c) {
                const std::string full = cellLabel(c);
                return full.substr(0, full.find(' '));
            };

            std::vector<std::array<LaneStat, kKinds>> stats(laneRenders);
            for (std::size_t r = 0; r < laneRenders; ++r) {
                const std::string label = (r == 0u) ? std::string("P")
                                                    : cellTag(laneCells[r - 1u]) + ".twin";
                for (std::size_t k = 0; k < kKinds; ++k) {
                    stats[r][k] = laneStat(traces[r][k]);
                    const LaneStat& s = stats[r][k];
                    std::printf("  lanes %s %s: mean %.4f p10 %.4f p90 %.4f dabs %.5f (blocks %zu)\n",
                                label.c_str(), kKindName[k], s.mean, s.p10, s.p90, s.dabs,
                                traces[r][k].size());
                }
            }
            for (std::size_t i = 0; i < laneCells.size(); ++i) {
                std::array<std::pair<double, std::size_t>, kKinds> score{};
                for (std::size_t k = 0; k < kKinds; ++k) {
                    const LaneStat& a = stats[0][k];
                    const LaneStat& b = stats[1u + i][k];
                    score[k] = {std::fabs(a.mean - b.mean) +
                                    std::fabs((a.p90 - a.p10) - (b.p90 - b.p10)),
                                k};
                }
                std::stable_sort(score.begin(), score.end(),
                                 [](const auto& x, const auto& y) { return x.first > y.first; });
                std::printf("  lanes rank %s:", cellTag(laneCells[i]).c_str());
                for (const auto& [sc, k] : score) {
                    std::printf(" %s=%.4f", kKindName[k], sc);
                }
                std::printf("\n");
            }
        }
    }

    // Phase 13c T010 (FR-004, FR-015, E-10, SC-005): VORAGO_PILOT_TAKES=4 prints
    // one arm line per take of A_K (the stored-take format above), then the
    // primary's d on every take: its twin(s), conjunct and route arms rendered at
    // that take's seed takeSeedIndex(stored, 0, j, 4) and scored against that
    // take's P_Sus, as the stored take is. Take 0 is the stored take, so its d is
    // the verdict line's. Reporting only: the "take:" and verdict lines above are
    // unchanged and remain the gate. computeTakes captures the D8.2 / D9.1 attack
    // window on take 0 only, so such a primary prints its skip on takes j > 0.
    if (takeCount > 1) {
        for (int j = 0; j < takeCount; ++j) {
            const VoragoTest::TakeRecord& t = p.takes[static_cast<std::size_t>(j)];
            std::printf("  take j=%d seed %d: peak %.4f  arm1 hi %.2f dB [%s]  arm2 lo %.2f dB [%s]  "
                        "arm3 late-sus %+.2f dB [%s]  arm4 tail %.2f dB [%s]\n",
                        j, t.seedIndex, static_cast<double>(t.peak), t.worstHiDb,
                        t.armPass[0] ? "yes" : "NO", t.worstLoDb, t.armPass[1] ? "yes" : "NO",
                        t.lateVsSusDb, t.armPass[2] ? "yes" : "NO", t.tailDb,
                        t.armPass[3] ? "yes" : "NO");
        }
        const auto takeN = static_cast<std::size_t>(takeCount);
        // Sized up front: every RenderSpec spans its take's comp, so no reallocation.
        std::vector<PilotPreset> takeP(takeN);
        std::vector<PilotPrimaryPlan> takePlans(takeN);
        std::vector<std::array<int, 4>> takeRoutes(takeN);
        std::vector<VoragoTest::RenderSpec> takeSpecs;
        for (std::size_t j = 1; j < takeN; ++j) {
            PilotPreset& pj = takeP[j];
            pj.label = p.label;
            pj.def = p.def;
            pj.comp = p.comp;
            pj.ready = true;
            pj.tl = p.tl;
            pj.storedSeed =
                VoragoTest::takeSeedIndex(p.storedSeed, 0, static_cast<int>(j), takeCount);
            pj.takes = {p.takes[j]};
            pj.selfDistance = p.selfDistance;
            takePlans[j] = planPrimary(pj, takeSpecs);
            takeRoutes[j] = planRoute(pj, takePlans[j], takeSpecs);
        }
        for (VoragoTest::RenderSpec& s : takeSpecs) {
            s.engineTweak = lever.tweak;  // T067: every other take's twins and route arms
        }
        std::vector<VoragoTest::SweepCapture> takeCaps(takeSpecs.size());
        std::vector<std::function<void()>> takeJobs;
        takeJobs.reserve(takeSpecs.size());
        for (std::size_t k = 0; k < takeSpecs.size(); ++k) {
            takeJobs.emplace_back(
                [&takeSpecs, &takeCaps, k] { takeCaps[k] = VoragoTest::renderPreset(takeSpecs[k]); });
        }
        VoragoTest::runJobs(takeJobs, width);
        for (const VoragoTest::SweepCapture& c : takeCaps) {
            REQUIRE(c.finite);
            REQUIRE(c.tweakHeld);
        }
        for (std::size_t j = 0; j < takeN; ++j) {
            if (j > 0u) {
                PilotPrimaryPlan& pl = takePlans[j];
                scoreTwin(takeP[j], pl, pl.twin, pl.attack, takeCaps, pl.o);
                scoreRoute(pl, takeRoutes[j], takeCaps, false);
                if (pl.conjCell != C::Count) {
                    scoreTwin(takeP[j], pl, pl.conjTwin, false, takeCaps, pl.conj);
                    pl.o.conjunctOk = VoragoTest::verifiedAt(pl.conj, PD::Verification::Ablation,
                                                             VoragoTest::ClaimRole::Secondary);
                    applyD3PrimaryRule(pl);
                }
            }
            const VoragoTest::CellOutcome& o = (j == 0u) ? plan.o : takePlans[j].o;
            std::printf("  take j=%zu d %.4f%s%s\n", j, o.d, o.skip.empty() ? "" : "  skip: ",
                        o.skip.c_str());
        }
    }
    std::fflush(stdout);
    PrimaryProbeResult result;
    result.d = plan.o.d;
    result.arms = p.takes.front().armPass;
    result.verified = verified;
    result.secondaries = std::move(secondaryRows);
    return result;
}

/// VORAGO_PILOT_OVERRIDE="id=norm,id=norm,...": the def with those settings
/// replaced (or appended when the def leaves the ID at its default), so a
/// re-author candidate is measured without a rebuild per variant (sweep 2
/// re-author loop, 2026-09-30). Phase 13d T019: a helper, so the roster applies
/// its kRosterGateOverrides strings through the same parser.
void applyPilotOverride(PD::VoragoPresetDef& patched, const std::string& ov) {
    std::size_t pos = 0;
    while (pos < ov.size()) {
        const std::size_t comma = ov.find(',', pos);
        const std::string item =
            ov.substr(pos, (comma == std::string::npos) ? std::string::npos : comma - pos);
        const std::size_t eq = item.find('=');
        REQUIRE(eq != std::string::npos);
        const auto id = static_cast<Steinberg::Vst::ParamID>(std::stoul(item.substr(0, eq)));
        const double v = std::stod(item.substr(eq + 1));
        bool replaced = false;
        for (PD::ParamSetting& st : patched.params) {
            if (st.id == id) {
                st.normalized = v;
                replaced = true;
            }
        }
        if (!replaced) {
            patched.params.push_back(PD::ParamSetting{.id = id, .normalized = v});
        }
        std::printf("[probe] override %u = %.6f (%s)\n", static_cast<unsigned>(id), v,
                    replaced ? "replaced" : "appended");
        if (comma == std::string::npos) {
            break;
        }
        pos = comma + 1u;
    }
}

/// Phase 13c T012 (FR-024c, SC-023, E-2): VORAGO_PILOT_MASTER_TRIM=<dB> scales
/// the patched def's kMasterGainId normalized value (stored, or 0.5 when the
/// def leaves it at its default) by 10^(dB/20), after the override and before
/// the state is built, so every render of the run (takes, twins, route arms,
/// 44.1 kHz) carries it. Phase 13d T019: a helper, shared with the roster.
void applyPilotMasterTrim(PD::VoragoPresetDef& patched, double trimDb) {
    const double factor = std::pow(10.0, trimDb / 20.0);
    double before = 0.5;
    bool present = false;
    for (PD::ParamSetting& st : patched.params) {
        if (st.id == ::Vorago::kMasterGainId) {
            before = st.normalized;
            st.normalized = before * factor;
            present = true;
        }
    }
    if (!present) {
        patched.params.push_back(
            PD::ParamSetting{.id = ::Vorago::kMasterGainId, .normalized = before * factor});
    }
    std::printf("[probe] master trim %g dB: kMasterGainId norm %.4f -> %.4f\n", trimDb, before,
                before * factor);
}

/// requiredPrimaryCells() minus the roster primaries, as compiled: the 27
/// sweep-4-verified cells (`sweep4`, Phase 13c T013, minus kRosterPrimaries) or
/// the 36 sweep-5-verified cells (Phase 13d FR-030, minus the six roster
/// primaries in kRosterGateOverrides; E6.hi / E7.hi are not required primaries).
std::vector<PD::Capability> derivedVerifiedPrimaries(bool sweep4) {
    std::vector<PD::Capability> out;
    for (const PD::Capability c : PD::requiredPrimaryCells()) {
        const bool roster =
            sweep4 ? std::find(kRosterPrimaries.begin(), kRosterPrimaries.end(), c) !=
                         kRosterPrimaries.end()
                   : std::any_of(kRosterGateOverrides.begin(), kRosterGateOverrides.end(),
                                 [c](const RosterGate& g) { return g.cell == c; });
        if (!roster) {
            out.push_back(c);
        }
    }
    return out;
}

/// Phase 13d T019 (FR-010c (b), plan 4.5 table): is the lever's path live in a
/// preset, from its stored normalized values? std::nullopt: an unknown lever id.
/// A value the map lacks is undecidable and counts as live (FR-010c). Every
/// tested parameter is normalized in [0, 1] with plain 0 at normalized 0
/// (ecosystem_params.h:38 / :66-69, bloom_params.h:35 / :67, ecology_params.h:72-73
/// linear [0, 1]; macros plain == normalized), so "> 0" is "away from 0".
std::optional<bool> presetLiveFor(std::string_view leverId,
                                  const std::map<Steinberg::Vst::ParamID, double>& stored) {
    const auto away = [&stored](Steinberg::Vst::ParamID id, double neutral) {
        const auto it = stored.find(id);
        return it == stored.end() || std::fabs(it->second - neutral) > 1e-9;
    };
    if (leverId == "e1route" || leverId == "e67") {
        // e1route: 900 > 0 and (1300 > 0 or the cloud sounds). Vorago has no cloud
        // level or mix parameter (plugin_ids.h:115-121), so whether the cloud
        // sounds is undecidable from stored values and counts as live: the
        // predicate reduces to 900 > 0, which is e67's.
        return away(::Vorago::kEcosystemDepthId, 0.0);
    }
    if (leverId == "e1attach") {
        return away(::Vorago::kBloomDepthId, 0.0);  // regardless of 900
    }
    if (leverId == "e4") {
        return away(::Vorago::kEcologyMixId, 0.0);
    }
    if (leverId == "m2") {
        return away(::Vorago::kMacroAgeId, 0.0);
    }
    if (leverId == "m4") {
        return away(::Vorago::kMacroMovementId, 0.0);
    }
    if (leverId == "m5") {
        return away(::Vorago::kMacroGravityId, 0.5);  // Gravity is bipolar
    }
    if (leverId == "m10") {
        return away(::Vorago::kMacroLifeId, 0.0);
    }
    return std::nullopt;
}

/// Phase 13d T019 (plan 4.5): one row per cell a probe run read -
/// `<mode> <cell> <host> <role> overrides <s|-> d bar arms [....] -> verified|no`.
/// The host's primary is role "primary" when it is in `verifiedPrimaries` and
/// "roster" otherwise; each secondary the run read is role "secondary". A row
/// reads "verified" iff its cell verifies at its role's bar AND the stored take's
/// arms 1-4 are green (FR-010d reads the four level arms with every gate).
void printProbeRows(const std::string& mode, const PD::VoragoPresetDef& host,
                    std::string_view overrides, const PrimaryProbeResult& r,
                    const std::vector<PD::Capability>& verifiedPrimaries) {
    const bool armsGreen = r.arms[0] && r.arms[1] && r.arms[2] && r.arms[3];
    const std::string hostName(host.name);
    const std::string ov = overrides.empty() ? std::string("-") : std::string(overrides);
    const auto row = [&](PD::Capability c, const char* role, double d, double bar, bool ok) {
        std::printf("%s %s %s %s overrides %s d %.4f bar %.4f arms [%s %s %s %s] -> %s\n",
                    mode.c_str(), cellLabel(c).c_str(), hostName.c_str(), role, ov.c_str(), d,
                    bar, r.arms[0] ? "y" : "n", r.arms[1] ? "y" : "n", r.arms[2] ? "y" : "n",
                    r.arms[3] ? "y" : "n", (ok && armsGreen) ? "verified" : "no");
    };
    const bool counted = std::find(verifiedPrimaries.begin(), verifiedPrimaries.end(),
                                   host.primary) != verifiedPrimaries.end();
    row(host.primary, counted ? "primary" : "roster", r.d, VoragoTest::kFloorF, r.verified);
    for (const PilotSecondaryRow& sr : r.secondaries) {
        row(sr.cell, "secondary", sr.d, VoragoTest::kSecondaryBar, sr.verified);
    }
    std::fflush(stdout);
}

}  // namespace

// =============================================================================
// Single-preset primary probe (gate G2 re-author loop, 2026-09-29)
// =============================================================================
// VORAGO_PILOT_PRESET=<name>: renders the named def's stored-seed take (with the
// attack window when its primary is D8.2 / D9.1), its primary twin and, for a
// StateWithS primary, the S conjunct twin, then prints d (or d_att / d_Sus), the
// bar and the verdict exactly as Vorago_PresetPilot_Calibrate scores them. Two
// or three renders, about 90 s: the re-author loop's instrument, so a candidate
// is measured before the 26-minute pilot confirms it. VORAGO_PILOT_OVERRIDE="id=norm,..."
// patches the def first (a candidate is measured without a rebuild). A
// RouteIsolated primary (E1-E5) renders its four route arms (plan 6.9) and is
// scored on d(R_k, R_k0) against attribBase d(R_0, R_00). Hidden; never a gate.
//
// Phase 13c T013 (FR-030, SC-007): VORAGO_PILOT_ITERATE=verified27 (no
// VORAGO_PILOT_PRESET needed) runs the same body, AS COMPILED (no override, no
// master trim), on the def whose primary is each of requiredPrimaryCells() minus
// kRosterPrimaries (27 cells), printing one verified27 line per cell; PASS iff
// the primary verifies and the stored take's arms 1-4 are green.
//
// Phase 13d T019 (FR-010c, FR-030, plan 4.5): VORAGO_PILOT_ITERATE also takes
//  - verified36: requiredPrimaryCells() minus the six roster primaries (36
//    cells, REQUIREd), as compiled - the same three prohibitions as verified27;
//  - liveFor:<id> (e1route, e1attach, e4, e67, m2, m4, m5, m10): every preset
//    whose stored values make the lever live (presetLiveFor), as compiled under
//    the current VORAGO_PILOT_LEVER, reading its primary when that is one of the
//    36 and each claimed secondary its frozen sweep-5 record verifies; no
//    override, no master trim; an unknown id FAILs;
//  - roster: each kRosterRuled cell on its gate surface (kRosterGateOverrides'
//    string, the current lever, and VORAGO_PILOT_MASTER_TRIM when set, for the
//    -6 dB re-read); E4 with four takes and the loop-bus meter, E6.hi / E7.hi on
//    the secondary path with S7. No VORAGO_PILOT_OVERRIDE.
// Every mode prints printProbeRows' rows; VORAGO_PILOT_LIST=1 prints the derived
// list and renders nothing.
TEST_CASE("Vorago_PresetPilot_PrimaryProbe", "[.probe][vorago]") {
    using C = PD::Capability;
    const unsigned width = e0PoolWidth();
    if (const std::optional<std::string> iter = VoragoTest::sweepEnv("VORAGO_PILOT_ITERATE")) {
        const std::string& mode = *iter;
        const bool listOnly =
            VoragoTest::sweepEnv("VORAGO_PILOT_LIST") == std::optional<std::string>("1");
        const std::vector<C> verified36 = derivedVerifiedPrimaries(false);
        REQUIRE(verified36.size() == 36u);
        const auto hostOf = [](C c) {
            const PD::VoragoPresetDef* host = nullptr;
            for (const PD::VoragoPresetDef& d : PD::allPresets()) {
                if (d.primary == c) {
                    host = &d;
                    break;
                }
            }
            return host;
        };

        if (mode == "verified27" || mode == "verified36") {
            REQUIRE(!VoragoTest::sweepEnv("VORAGO_PILOT_OVERRIDE").has_value());
            REQUIRE(!VoragoTest::sweepEnv("VORAGO_PILOT_MASTER_TRIM").has_value());
            REQUIRE(!VoragoTest::sweepEnv("VORAGO_PILOT_LEVER").has_value());  // T067: as compiled
            const bool sweep4 = (mode == "verified27");
            const std::vector<C> derived = sweep4 ? derivedVerifiedPrimaries(true) : verified36;
            REQUIRE(derived.size() == (sweep4 ? 27u : 36u));
            std::printf("%s: %zu cells\n", mode.c_str(), derived.size());
            for (const C c : derived) {
                const PD::VoragoPresetDef* host = hostOf(c);
                REQUIRE(host != nullptr);
                const std::string presetName(host->name);
                if (listOnly) {
                    std::printf("%s list %s %s\n", mode.c_str(), cellLabel(c).c_str(),
                                presetName.c_str());
                    continue;
                }
                const PrimaryProbeResult r =
                    runPrimaryProbe(*host, presetName, width, PilotLever{});
                printProbeRows(mode, *host, {}, r, derived);
            }
            std::fflush(stdout);
            return;
        }

        if (mode == "roster") {
            // Each cell's override comes from kRosterGateOverrides; a global one
            // would mix surfaces.
            REQUIRE(!VoragoTest::sweepEnv("VORAGO_PILOT_OVERRIDE").has_value());
            const PilotLever lever = readPilotLever();
            const std::optional<std::string> trim =
                VoragoTest::sweepEnv("VORAGO_PILOT_MASTER_TRIM");
            std::printf("roster: %zu ruled cell(s), lever: %s\n", kRosterRuled.size(),
                        lever.text.c_str());
            std::vector<const RosterGate*> done;
            for (const C cell : kRosterRuled) {
                const RosterGate* gate = nullptr;
                for (const RosterGate& g : kRosterGateOverrides) {
                    if (g.cell == cell) {
                        gate = &g;
                    }
                }
                REQUIRE(gate != nullptr);
                // E6.hi / E7.hi share one surface: render the host once.
                const auto same =
                    std::find_if(done.begin(), done.end(),
                                 [gate](const RosterGate* g) { return g->host == gate->host; });
                if (same != done.end()) {
                    REQUIRE((*same)->overrides == gate->overrides);
                    continue;
                }
                done.push_back(gate);
                const PD::VoragoPresetDef* host = findPilotDef(gate->host);
                REQUIRE(host != nullptr);
                PilotProbeOptions opts;
                opts.fourTakes = (cell == C::E4FeedbackLoopWake);
                opts.loopBus = opts.fourTakes;
                if (cell == C::E6SyncRateHi || cell == C::E7SelfAffinityHi) {
                    std::vector<C> cells(host->secondaries.begin(), host->secondaries.end());
                    if (std::find(cells.begin(), cells.end(), C::S7Ecosystem) == cells.end()) {
                        cells.push_back(C::S7Ecosystem);
                    }
                    opts.secondaryCells = std::move(cells);
                }
                const std::string presetName(host->name);
                const std::string ov(gate->overrides);
                if (listOnly) {
                    std::printf("roster list %s %s overrides %s%s%s\n", cellLabel(cell).c_str(),
                                presetName.c_str(), ov.empty() ? "-" : ov.c_str(),
                                opts.fourTakes ? " takes 4 loopbus" : "",
                                opts.secondaryCells.has_value() ? " secondaries+S7" : "");
                    continue;
                }
                PD::VoragoPresetDef patched = *host;
                if (!ov.empty()) {
                    applyPilotOverride(patched, ov);
                }
                if (trim.has_value()) {
                    applyPilotMasterTrim(patched, std::stod(*trim));
                }
                const PrimaryProbeResult r =
                    runPrimaryProbe(patched, presetName, width, lever, opts);
                printProbeRows(mode, *host, gate->overrides, r, verified36);
            }
            std::fflush(stdout);
            return;
        }

        constexpr std::string_view kLiveForPrefix = "liveFor:";
        if (mode.starts_with(kLiveForPrefix)) {
            const std::string leverId = mode.substr(kLiveForPrefix.size());
            if (!presetLiveFor(leverId, {}).has_value()) {
                FAIL("unknown liveFor lever id " << leverId);
            }
            // The (b) set is read override-free (plan 4.5).
            REQUIRE(!VoragoTest::sweepEnv("VORAGO_PILOT_OVERRIDE").has_value());
            REQUIRE(!VoragoTest::sweepEnv("VORAGO_PILOT_MASTER_TRIM").has_value());
            const PilotLever lever = readPilotLever();
            std::printf("%s: lever: %s\n", mode.c_str(), lever.text.c_str());
            const std::vector<PD::VoragoPresetDef>& defs = PD::allPresets();
            std::size_t liveCount = 0;
            for (std::size_t i = 0; i < defs.size(); ++i) {
                const PD::VoragoPresetDef& def = defs[i];
                std::vector<std::uint8_t> comp;
                std::string why;
                REQUIRE(VoragoTest::buildPresetComponentState(def, comp, why));
                const std::map<Steinberg::Vst::ParamID, double> stored =
                    VoragoTest::storedNormalizedValues(std::span<const std::uint8_t>(comp));
                if (!presetLiveFor(leverId, stored).value_or(true)) {
                    continue;
                }
                ++liveCount;
                // The claimed secondaries its frozen sweep-5 record verifies (FR-030b).
                VoragoTest::SweepRecord rec;
                const std::filesystem::path recPath =
                    VoragoTest::sweepRecordPath(std::string(kSweep5RecordDir), i);
                INFO("sweep-5 record " << recPath.string());
                REQUIRE(VoragoTest::readRecord(recPath, rec));
                REQUIRE(std::string_view(rec.name) == def.name);
                std::vector<C> secs;
                for (const C c : def.secondaries) {
                    if (VoragoTest::verifiedAt(rec.vec.cells[static_cast<std::size_t>(c)], c,
                                               VoragoTest::ClaimRole::Secondary)) {
                        secs.push_back(c);
                    }
                }
                const bool primaryCounts =
                    std::find(verified36.begin(), verified36.end(), def.primary) !=
                    verified36.end();
                const std::string presetName(def.name);
                if (listOnly) {
                    std::printf("%s list %s primary %s%s secondaries", mode.c_str(),
                                presetName.c_str(), cellLabel(def.primary).c_str(),
                                primaryCounts ? "" : " (roster)");
                    for (const C c : secs) {
                        std::printf(" [%s]", cellLabel(c).c_str());
                    }
                    std::printf("\n");
                    continue;
                }
                if (!primaryCounts && secs.empty()) {
                    std::printf("%s skip %s: roster primary, no sweep-5-verified secondary\n",
                                mode.c_str(), presetName.c_str());
                    continue;
                }
                PilotProbeOptions opts;
                opts.secondaryCells = secs;
                const PrimaryProbeResult r = runPrimaryProbe(def, presetName, width, lever, opts);
                printProbeRows(mode, def, {}, r, verified36);
            }
            std::printf("%s: %zu live preset(s)\n", mode.c_str(), liveCount);
            std::fflush(stdout);
            return;
        }

        FAIL("unknown VORAGO_PILOT_ITERATE value " << mode);
    }

    const std::optional<std::string> name = VoragoTest::sweepEnv("VORAGO_PILOT_PRESET");
    REQUIRE(name.has_value());

    const PD::VoragoPresetDef* found = findPilotDef(*name);
    REQUIRE(found != nullptr);
    // VORAGO_PILOT_OVERRIDE (applyPilotOverride) then VORAGO_PILOT_MASTER_TRIM
    // (applyPilotMasterTrim). Absent: the def as compiled.
    PD::VoragoPresetDef patched = *found;
    if (const std::optional<std::string> ov = VoragoTest::sweepEnv("VORAGO_PILOT_OVERRIDE")) {
        applyPilotOverride(patched, *ov);
    }
    if (const std::optional<std::string> trim = VoragoTest::sweepEnv("VORAGO_PILOT_MASTER_TRIM")) {
        applyPilotMasterTrim(patched, std::stod(*trim));
    }
    runPrimaryProbe(patched, *name, width, readPilotLever());
}

// =============================================================================
// Phase 13c T031 diagnostic (hidden, never a gate): the DESTINATION ceiling of a
// route. Renders the named preset twice at its stored seed over the sustain twin
// window - as stored, and with VORAGO_PILOT_SWING="id=norm,..." applied at
// block 0 - and prints d(P, P_swing) on the C-7.2 descriptor. A route lever can
// never move its cell's d(R_k, R_k0) further than the swing of the parameters it
// writes, so this bounds a ladder before any rung is built.
// =============================================================================
TEST_CASE("Vorago_PresetPilot_SwingProbe", "[.probe][vorago]") {
    const std::optional<std::string> nameEnv = VoragoTest::sweepEnv("VORAGO_PILOT_PRESET");
    REQUIRE(nameEnv.has_value());
    const std::string name = nameEnv.value_or("");
    const std::optional<std::string> swingEnv = VoragoTest::sweepEnv("VORAGO_PILOT_SWING");
    REQUIRE(swingEnv.has_value());
    const std::string swing = swingEnv.value_or("");
    const PD::VoragoPresetDef* def = findPilotDef(name);
    REQUIRE(def != nullptr);
    std::vector<std::uint8_t> comp;
    std::string why;
    REQUIRE(VoragoTest::buildPresetComponentState(*def, comp, why));
    VoragoTest::SweepTimeline tl{};
    int storedSeed = 0;
    REQUIRE(pilotDecodeTimeline(std::span<const std::uint8_t>(comp), tl, storedSeed));
    VoragoTest::ParamOverrides ov;
    std::size_t pos = 0;
    while (pos < swing.size()) {
        const std::size_t comma = swing.find(',', pos);
        const std::string item =
            swing.substr(pos, (comma == std::string::npos) ? std::string::npos : comma - pos);
        const std::size_t eq = item.find('=');
        REQUIRE(eq != std::string::npos);
        ov.emplace_back(static_cast<Steinberg::Vst::ParamID>(std::stoul(item.substr(0, eq))),
                        std::stod(item.substr(eq + 1)));
        if (comma == std::string::npos) {
            break;
        }
        pos = comma + 1u;
    }
    const std::span<const std::uint8_t> span(comp);
    const VoragoTest::SweepCapture base =
        VoragoTest::renderPreset(VoragoTest::detail::twinSusSpec(span, tl, storedSeed, {}));
    const VoragoTest::SweepCapture swung =
        VoragoTest::renderPreset(VoragoTest::detail::twinSusSpec(span, tl, storedSeed, ov));
    const std::optional<VoragoTest::PresetDescriptor> dBase =
        VoragoTest::detail::twinSusDescriptor(base, 0u, VoragoTest::kSweepSampleRate);
    const std::optional<VoragoTest::PresetDescriptor> dSwing =
        VoragoTest::detail::twinSusDescriptor(swung, 0u, VoragoTest::kSweepSampleRate);
    REQUIRE(dBase.has_value());
    REQUIRE(dSwing.has_value());
    std::printf("[swing] %s seed %d swing \"%s\": d(P, P_swing) %.4f  (peak P %.4f, P_swing %.4f)\n",
                name.c_str(), storedSeed, swing.c_str(),
                VoragoTest::descriptorDistance(*dBase, *dSwing),
                static_cast<double>(base.peak), static_cast<double>(swung.peak));
    REQUIRE(true);
}
