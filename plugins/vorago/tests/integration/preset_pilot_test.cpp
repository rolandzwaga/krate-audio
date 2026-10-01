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
            if (p.takes.empty() || !p.takes.front().attack.has_value() || !wEnd.has_value() ||
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
        !storedTake.attack.has_value()) {
        VoragoTest::markSkipped(o, std::string(VoragoTest::kSkipRenderFailed));
        return;
    }
    const double floorDb = storedTake.susDb - VoragoTest::kAttackFloorBelowSusDb;
    const VoragoTest::PresetDescriptor attRev =
        VoragoTest::describeWithEnergyFloor(cap.capL[0], cap.capR[0], kSr, floorDb);
    o.rendered = true;
    o.d = VoragoTest::descriptorDistance(*storedTake.attack, attRev);
    o.attribBase = VoragoTest::descriptorDistance(pSus, *susRev);

    const double susRevDb = VoragoTest::detail::spanDb(cap, plan.susRev0, plan.susRev1, kSr);
    std::printf("  attack window (W_end %.1f s): d_att %.4f, d_Sus %.4f (attributable iff "
                "d_att >= %.4f)\n",
                plan.wEnd, o.d, o.attribBase, o.attribBase + VoragoTest::kAttribMargin);
    VoragoTest::detail::printReach("P", storedTake.attackReachSeconds);
    VoragoTest::detail::printReach(
        "P_rev", VoragoTest::detail::firstSecondAtOrAbove(
                     cap.capL[0], cap.capR[0], kSr, susRevDb - VoragoTest::kAttackReachBelowSusDb));
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
TEST_CASE("Vorago_PresetPilot_PrimaryProbe", "[.probe][vorago]") {
    using C = PD::Capability;
    const std::optional<std::string> name = VoragoTest::sweepEnv("VORAGO_PILOT_PRESET");
    REQUIRE(name.has_value());
    const unsigned width = e0PoolWidth();

    PilotPreset p;
    p.label = *name;
    const PD::VoragoPresetDef* found = findPilotDef(*name);
    REQUIRE(found != nullptr);
    // VORAGO_PILOT_OVERRIDE="id=norm,id=norm,...": the named def with those
    // settings replaced (or appended when the def leaves the ID at its default),
    // so a re-author candidate is measured without a rebuild per variant
    // (sweep 2 re-author loop, 2026-09-30). Absent: the def as compiled.
    PD::VoragoPresetDef patched = *found;
    if (const std::optional<std::string> ov = VoragoTest::sweepEnv("VORAGO_PILOT_OVERRIDE")) {
        std::size_t pos = 0;
        while (pos < ov->size()) {
            const std::size_t comma = ov->find(',', pos);
            const std::string item =
                ov->substr(pos, (comma == std::string::npos) ? std::string::npos : comma - pos);
            const std::size_t eq = item.find('=');
            REQUIRE(eq != std::string::npos);
            const auto id = static_cast<Steinberg::Vst::ParamID>(std::stoul(item.substr(0, eq)));
            const double v = std::stod(item.substr(eq + 1));
            bool replaced = false;
            for (PD::ParamSetting& s : patched.params) {
                if (s.id == id) {
                    s.normalized = v;
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
    p.def = &patched;
    REQUIRE(VoragoTest::buildPresetComponentState(*p.def, p.comp, p.why));
    REQUIRE(pilotDecodeTimeline(std::span<const std::uint8_t>(p.comp), p.tl, p.storedSeed));
    p.ready = true;
    p.takes = VoragoTest::computeTakes(
        p.comp, p.tl, p.storedSeed, 1, width,
        VoragoTest::attackWindowEndSeconds(p.def, std::span<const std::uint8_t>(p.comp)));
    REQUIRE(p.takes.size() == 1u);
    REQUIRE(p.takes.front().finite);

    std::vector<VoragoTest::RenderSpec> specs;
    PilotPrimaryPlan plan = planPrimary(p, specs);
    // Route primaries (E1-E5, plan 6.9; sweep-2 re-author loop 2026-09-30): the
    // pilot planner skips RouteIsolated, so the probe adds the four route arms
    // itself - R_k (P + the other four destinations' S overrides), R_k0 (+ depth
    // 0), R_0 and R_00 - and scores d = d(R_k, R_k0), attribBase = d(R_0, R_00)
    // exactly as computeVerificationVector's pass 2 does.
    int routeK = -1;
    int routeK0 = -1;
    int routeNull = -1;
    int routeNull0 = -1;
    if (PD::cellSpecs()[static_cast<std::size_t>(plan.cell)].verification ==
        PD::Verification::RouteIsolated) {
        VoragoTest::DecodedPresetState st;
        REQUIRE(VoragoTest::decodePresetState(std::span<const std::uint8_t>(p.comp), st));
        const std::map<Steinberg::Vst::ParamID, double> stored =
            VoragoTest::storedNormalizedValues(std::span<const std::uint8_t>(p.comp));
        const std::string why = VoragoTest::skipReason(plan.cell, st, stored,
                                                       VoragoTest::routeOverrides(plan.cell));
        if (!why.empty()) {
            VoragoTest::markSkipped(plan.o, why);
        } else {
            const std::span<const std::uint8_t> comp(p.comp);
            const auto push = [&](VoragoTest::ParamOverrides ov) {
                specs.push_back(
                    VoragoTest::detail::twinSusSpec(comp, p.tl, p.storedSeed, std::move(ov)));
                return static_cast<int>(specs.size()) - 1;
            };
            routeK = push(VoragoTest::routeOverrides(plan.cell));
            routeK0 = push(VoragoTest::routeOverrides(plan.cell, true));
            routeNull = push(VoragoTest::routeOverrides(C::Count));
            routeNull0 = push(VoragoTest::routeOverrides(C::Count, true));
            plan.o.stateOk = true;
            plan.o.conjunctOk = true;
            plan.o.skip.clear();
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
    }

    const std::string label = cellLabel(plan.cell);
    std::printf("[probe] %s - primary %s (stored seed %d, %zu twin render(s))\n", p.label.c_str(),
                label.c_str(), p.storedSeed, specs.size());
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
    scoreTwin(p, plan, plan.twin, plan.attack, caps, plan.o);
    if (routeK >= 0) {
        constexpr double kSr = VoragoTest::kSweepSampleRate;
        const auto desc = [&](int index) {
            return VoragoTest::detail::twinSusDescriptor(caps[static_cast<std::size_t>(index)], 0u,
                                                         kSr);
        };
        const auto dK = desc(routeK);
        const auto dK0 = desc(routeK0);
        const auto dNull = desc(routeNull);
        const auto dNull0 = desc(routeNull0);
        if (!dK || !dK0 || !dNull || !dNull0) {
            VoragoTest::markSkipped(plan.o, std::string(VoragoTest::kSkipRenderFailed));
        } else {
            plan.o.rendered = true;
            plan.o.d = VoragoTest::descriptorDistance(*dK, *dK0);
            plan.o.attribBase = VoragoTest::descriptorDistance(*dNull, *dNull0);
            std::printf("  route arms: d(R_k, R_k0) %.4f, attribBase d(R_0, R_00) %.4f (attributable "
                        "iff d >= %.4f)\n",
                        plan.o.d, plan.o.attribBase, plan.o.attribBase + VoragoTest::kAttribMargin);
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
    std::printf("  primary d %.4f  bar %.4f  attribBase %.4f  state %s  -> %s%s%s\n", plan.o.d,
                VoragoTest::kFloorF, plan.o.attribBase, plan.o.stateOk ? "ok" : "false",
                verified ? "PASS" : "FAIL", plan.o.skip.empty() ? "" : "  skip: ",
                plan.o.skip.c_str());
    std::fflush(stdout);
}
