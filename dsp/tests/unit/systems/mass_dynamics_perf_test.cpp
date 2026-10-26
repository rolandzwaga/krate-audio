// ==============================================================================
// Layer 3: System Tests - Mass dynamics, perf TU
//                       (Profundum Phase 6, specs/profundum-phase6-mass-dynamics)
// ==============================================================================
// Reference: specs/profundum-phase6-mass-dynamics/spec.md SC-017 (FR-045; user ruling
//            2026-10-17, Clarifications "Plan S1 rulings"), plan.md S12.4 / R-1 (T001 measured
//            0.7694 % pinned), tasks.md T018 (deviation 9).
//
// SCOPE: SC-017 (MassDynamics_CpuBudget) [performance][.perf]. Deliberately NOT in the
//   -fno-fast-math block: its figures must reflect the shipping FP mode. FTZ/DAZ is on under
//   the test main (dsp_test_main.cpp). Run alone and pinned, nothing else executing:
//   bash specs/profundum-phase6-mass-dynamics/evidence/close/cpu-lane.sh
//   (= pwsh -NoProfile -File tools/pin-perf-cores.ps1
//        -Exe build/windows-x64-release/bin/Release/dsp_systems_tests.exe
//        -ExeArgs "MassDynamics_CpuBudget")
//
// GATED: 48 kHz, host block 64, a DISTINCT L / R kSawAnchor E1 pair (R = L delayed by
//   round(0.37 * P) samples, both cut from one longer render); MassDynamics at attack +12 dB,
//   sustain -6 dB, duration 40 ms, compression amount 1, detector key E1; plus one
//   MassLaw::advanceInterval() + MassLaw::evaluate per 32-sample interval (Mass held at 75: the
//   evaluation has no fast path, so its cost does not depend on the value; T001 bench).
//   10 s of audio per repetition, best of 5, std::chrono::steady_clock;
//   % = wall / audio x 100 <= 1.0 % of one core.
// RECORDED (printed and written to evidence/sc017-cpu.txt, not gated): DCBlocker x2 +
//   TruePeakLimiter on the same pair (for Phase 9); the L == R cell (MassDynamics in its mono
//   path, right == left, the result copied to R); one silent second through the gated chain
//   after one second of signal.
// ==============================================================================

#include "../processors/transient_shaper_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/processors/transient_shaper.h>
#include <krate/dsp/processors/true_peak_limiter.h>
#include <krate/dsp/systems/mass_dynamics.h>
#include <krate/dsp/systems/mass_law.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace Krate::DSP;
namespace mdpt = Krate::DSP::TransientShaperTest;

namespace {

constexpr double kMdpFs = 48000.0;
constexpr std::size_t kMdpBlock = 64;                             // host block
constexpr std::size_t kMdpInterval = MassLaw::kControlInterval;   // 32
constexpr double kMdpSeconds = 10.0;                              // audio per timed repetition
constexpr int kMdpReps = 5;                                       // best of 5
constexpr std::size_t kMdpBlocksPerSecond = 750;                  // 48000 / 64
constexpr std::size_t kMdpWarmupBlocks = kMdpBlocksPerSecond;     // 1 s, untimed
constexpr double kMdpLoopPeriods = 41.0;      // input loop: whole E1 periods (wrap error < 0.05 sample)
constexpr double kMdpROffsetPeriods = 0.37;   // R = L delayed by round(0.37 * P) (SC-017)
constexpr float kMdpAttackDb = 12.0f;
constexpr float kMdpSustainDb = -6.0f;
constexpr float kMdpDurationMs = 40.0f;
constexpr float kMdpAmount = 1.0f;
constexpr float kMdpMass = 75.0f;
constexpr double kMdpBarPct = 1.0;            // SC-017 bar, % of one core

static_assert(kMdpBlock % kMdpInterval == 0, "the host block holds whole control intervals");
static_assert(kMdpInterval == MassDynamics::kControlInterval, "one grid for MassLaw and MassDynamics");
static_assert(kMdpBlocksPerSecond * kMdpBlock == 48000, "750 blocks of 64 = 1 s at 48 kHz");

enum class MdpCell : std::uint8_t { Gated, DcLimiter, Linked };

/// The input loop pair: L = kSawAnchor E1 at -12 dBFS RMS, R = L delayed by round(0.37 * P).
struct MdpInput {
    std::vector<float> left;
    std::vector<float> right;
    std::size_t shift = 0;
};

MdpInput mdpMakeInput() {
    MdpInput in;
    const double periodSamples = kMdpFs / mdpt::kE1Hz;
    const auto loopLen = static_cast<std::size_t>(std::llround(kMdpLoopPeriods * periodSamples));
    in.shift = static_cast<std::size_t>(std::llround(kMdpROffsetPeriods * periodSamples));
    const double seconds = static_cast<double>(loopLen + in.shift + 1) / kMdpFs;
    const std::vector<float> fx = mdpt::steadyTone(mdpt::Tone::Saw, mdpt::kE1Hz, kMdpFs, seconds);
    REQUIRE(fx.size() >= loopLen + in.shift);
    // L[n] = fx[n + shift], R[n] = fx[n] = L[n - shift]: R is L delayed by `shift` samples.
    in.left.assign(fx.begin() + static_cast<std::ptrdiff_t>(in.shift),
                   fx.begin() + static_cast<std::ptrdiff_t>(in.shift + loopLen));
    in.right.assign(fx.begin(), fx.begin() + static_cast<std::ptrdiff_t>(loopLen));
    return in;
}

/// The gated composition and the recorded stages. Heap-held, prepared outside the timed region.
struct MdpRig {
    MassDynamics md;
    MassLaw law;
    std::array<DCBlocker, 2> dc{};
    TruePeakLimiter limiter;
    MassLaw::Targets lastTargets{};

    void prepare() {
        // Shaper gains before prepare: prepare applies them and its reset() snaps the 10 ms
        // smoothers (mass_dynamics_test_helpers.h, T011 recorded decision).
        md.setTransientAttackDb(kMdpAttackDb);
        md.setTransientSustainDb(kMdpSustainDb);
        md.setTransientDurationMs(kMdpDurationMs);
        md.setCompressionAmount(kMdpAmount);
        md.setDetectorKeyHz(static_cast<float>(mdpt::kE1Hz));
        md.prepare(kMdpFs, kMdpBlock);
        law.setMass(kMdpMass);
        law.prepare(kMdpFs);   // prepare -> reset() snaps the smoothed Mass to 75
        for (auto& d : dc)
            d.prepare(kMdpFs);
        limiter.prepare(kMdpFs, kMdpBlock);
    }

    /// One MassLaw step + evaluation per 32-sample interval of the host block.
    void lawBlock() noexcept {
        for (std::size_t off = 0; off < kMdpBlock; off += kMdpInterval) {
            const float m = law.advanceInterval();
            lastTargets = MassLaw::evaluate(m, law.base());
        }
    }
};

struct MdpBuffers {
    std::array<float, kMdpBlock> l{};
    std::array<float, kMdpBlock> r{};
    std::size_t pos = 0;

    void fill(const MdpInput& in) noexcept {
        const std::size_t loopLen = in.left.size();
        for (std::size_t i = 0; i < kMdpBlock; ++i) {
            l[i] = in.left[pos];
            r[i] = in.right[pos];
            if (++pos >= loopLen)
                pos = 0;
        }
    }
};

/// Runs `cell`: prepare, 1 s untimed warm-up, kMdpReps timed repetitions of kMdpSeconds.
/// Returns min(wall) / audio x 100 (% of one core).
double mdpMeasure(MdpCell cell, const MdpInput& in, double& checksum) {
    auto rig = std::make_unique<MdpRig>();
    rig->prepare();
    MdpBuffers buf;

    const auto block = [&]() {
        buf.fill(in);
        float* l = buf.l.data();
        float* r = buf.r.data();
        switch (cell) {
            case MdpCell::Gated:
                rig->lawBlock();
                rig->md.processBlock(l, r, kMdpBlock);
                break;
            case MdpCell::Linked:
                rig->lawBlock();
                rig->md.processBlock(l, l, kMdpBlock);   // mono path (right == left)
                std::memcpy(r, l, kMdpBlock * sizeof(float));
                break;
            case MdpCell::DcLimiter:
                for (std::size_t i = 0; i < kMdpBlock; ++i) {
                    l[i] = rig->dc[0].process(l[i]);
                    r[i] = rig->dc[1].process(r[i]);
                }
                rig->limiter.processBlock(l, r, static_cast<int>(kMdpBlock));
                break;
        }
        checksum += static_cast<double>(l[kMdpBlock - 1]) + static_cast<double>(r[kMdpBlock - 1]);
    };

    for (std::size_t b = 0; b < kMdpWarmupBlocks; ++b)
        block();

    const auto blocksPerRep = static_cast<std::size_t>(std::llround(kMdpSeconds * kMdpFs)) / kMdpBlock;
    const double audioSeconds = static_cast<double>(blocksPerRep * kMdpBlock) / kMdpFs;
    double best = std::numeric_limits<double>::infinity();
    for (int rep = 0; rep < kMdpReps; ++rep) {
        const auto t0 = std::chrono::steady_clock::now();
        for (std::size_t b = 0; b < blocksPerRep; ++b)
            block();
        const auto t1 = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double>(t1 - t0).count() / audioSeconds * 100.0);
    }
    if (cell != MdpCell::DcLimiter)
        REQUIRE(rig->md.stateFinite());
    checksum += static_cast<double>(rig->lastTargets.transientDurationMs);
    return best;
}

/// One silent second (plan R-8): per repetition, 1 s of signal (untimed) then exactly 1 s of
/// digital silence (timed) through the gated chain. Returns min(wall) / audio x 100 over kMdpReps.
double mdpMeasureSilence(const MdpInput& in, double& checksum) {
    auto rig = std::make_unique<MdpRig>();
    rig->prepare();
    MdpBuffers buf;
    const double audioSeconds = static_cast<double>(kMdpBlocksPerSecond * kMdpBlock) / kMdpFs;
    double best = std::numeric_limits<double>::infinity();
    for (int rep = 0; rep < kMdpReps; ++rep) {
        for (std::size_t b = 0; b < kMdpBlocksPerSecond; ++b) {
            buf.fill(in);
            rig->lawBlock();
            rig->md.processBlock(buf.l.data(), buf.r.data(), kMdpBlock);
        }
        const auto t0 = std::chrono::steady_clock::now();
        for (std::size_t b = 0; b < kMdpBlocksPerSecond; ++b) {
            buf.l.fill(0.0f);
            buf.r.fill(0.0f);
            rig->lawBlock();
            rig->md.processBlock(buf.l.data(), buf.r.data(), kMdpBlock);
            checksum += static_cast<double>(buf.l[kMdpBlock - 1]);
        }
        const auto t1 = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double>(t1 - t0).count() / audioSeconds * 100.0);
    }
    REQUIRE(rig->md.stateFinite());
    checksum += static_cast<double>(rig->lastTargets.transientDurationMs);
    return best;
}

std::string mdpEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase6-mass-dynamics/evidence/";
}

/// Prints a line and appends it to the evidence log.
void mdpEmit(std::ostringstream& log, const char* line) {
    std::printf("%s\n", line);
    log << line << '\n';
}

}  // namespace

// SC-017 (FR-045). Do NOT run concurrently with anything else; run pinned via evidence/close/cpu-lane.sh.
TEST_CASE("MassDynamics_CpuBudget", "[systems][profundum][mass-dynamics][performance][.perf]") {
    const MdpInput in = mdpMakeInput();
    double checksum = 0.0;
    std::ostringstream log;
    char line[400];

    std::snprintf(line, sizeof(line),
                  "SC017 setup fs=%.0f block=%zu interval=%zu note=E1 (%.5f Hz) fixture=kSawAnchor -12 dBFS RMS "
                  "loop=%zu samples R_delay=%zu samples (0.37 P) reps=best of %d x %.1f s warmup=1 s bar=%.1f%%",
                  kMdpFs, kMdpBlock, kMdpInterval, mdpt::kE1Hz, in.left.size(), in.shift, kMdpReps, kMdpSeconds,
                  kMdpBarPct);
    mdpEmit(log, line);

    // ---- Gated cell ----
    const double gatedPct = mdpMeasure(MdpCell::Gated, in, checksum);
    std::snprintf(line, sizeof(line),
                  "SC017 GATED distinct_pair MassDynamics(A=+%.0f dB S=%.0f dB duration=%.0f ms amount=%.0f key=E1) "
                  "+ MassLaw advanceInterval+evaluate per %zu samples (Mass %.0f) pct=%.4f bar=%.1f",
                  static_cast<double>(kMdpAttackDb), static_cast<double>(kMdpSustainDb),
                  static_cast<double>(kMdpDurationMs), static_cast<double>(kMdpAmount), kMdpInterval,
                  static_cast<double>(kMdpMass), gatedPct, kMdpBarPct);
    mdpEmit(log, line);

    // ---- Recorded cells (not gated) ----
    const double dcLimPct = mdpMeasure(MdpCell::DcLimiter, in, checksum);
    std::snprintf(line, sizeof(line),
                  "SC017 RECORD dc_limiter DCBlocker x2 + TruePeakLimiter (Phase 9) pct=%.4f (not gated)", dcLimPct);
    mdpEmit(log, line);

    const double linkedPct = mdpMeasure(MdpCell::Linked, in, checksum);
    std::snprintf(line, sizeof(line),
                  "SC017 RECORD linked L == R (MassDynamics mono path, right == left, copied to R) pct=%.4f "
                  "(not gated)",
                  linkedPct);
    mdpEmit(log, line);

    const double silencePct = mdpMeasureSilence(in, checksum);
    std::snprintf(line, sizeof(line),
                  "SC017 RECORD silence one silent second after 1 s of signal (gated chain) pct=%.4f (not gated)",
                  silencePct);
    mdpEmit(log, line);

    std::snprintf(line, sizeof(line), "SC017 VERDICT %s %.3f %%", gatedPct <= kMdpBarPct ? "PASS" : "FAIL",
                  gatedPct);
    mdpEmit(log, line);

    const std::string dir = mdpEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream out(dir + "sc017-cpu.txt");
    REQUIRE(out.is_open());
    out << log.str();
    out.close();

    REQUIRE(detail::isFinite(checksum));   // keeps the render from being elided
    CHECK(gatedPct <= kMdpBarPct);         // SC-017: <= 1.0 % of one core
}
