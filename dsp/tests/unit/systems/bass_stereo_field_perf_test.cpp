// ==============================================================================
// Layer 3: System Tests - BassStereoField, perf TU
//                          (Profundum Phase 7, specs/profundum-phase7-stereo-field)
// ==============================================================================
// Reference: specs/profundum-phase7-stereo-field/spec.md SC-013 (FR-079), plan.md S12.6 / R-1 (T001
//            measured per voice 0.1979 %, four voices 0.6920 %, stage 0.0732 / 0.0946 % pinned, with the
//            bench prototype), tasks.md T014 (deviation 9).
//
// SCOPE: SC-013 (BassStereoField_Cpu) [performance][.perf]. Deliberately NOT in the -fno-fast-math
//   block: its figures must reflect the shipping FP mode. FTZ/DAZ is on under the test main
//   (dsp_test_main.cpp). Run alone and pinned, nothing else executing:
//   bash specs/profundum-phase7-stereo-field/evidence/close/cpu-lane.sh
//   (= pwsh -NoProfile -File tools/pin-perf-cores.ps1
//        -Exe build/windows-x64-release/bin/Release/dsp_systems_tests.exe
//        -ExeArgs "BassStereoField_Cpu")
//
// Common: 48 kHz, host block 64 split on the 32-sample control grid (advanceControl() before each
//   interval's queries and processBlock; plan S1 C-7), std::chrono::steady_clock, best of 5 timed
//   repetitions after a 1 s untimed warm-up, % = wall / audio x 100 (% of one core).
//
// GATED:
//   (a)       the stage at Gravity 1, SPREAD, Spread 1, MOTION depth 1 rate 1 on the dearer source (a
//             first timing pass of both picks it), crossover 120 Hz, advanceControl every interval, a
//             DISTINCT L / R kSawAnchor E1 pair (a ProfundumCore render; R = L delayed by round(0.37 P)),
//             60 s of audio per repetition: <= 0.5 %.
//   (a-sweep) (a) with the crossover control a triangle 60 <-> 250 Hz, 100 ms legs: <= 0.5 % at 48 kHz.
//   (b)       one and four ProfundumCores (E1; E1, E2, C3, A1), kSawAnchor, 64 partials, Spread 1, MOTION
//             depth 1 rate 1 (each source), one global advanceControl + one computePanVector +
//             setPartialPanOffsets per voice per interval, minus the same at Spread 0 (tasks.md T014):
//             the worse source <= 0.25 % (one voice) and <= 1.0 % (four voices).
// RECORDED (printed and written to evidence/sc013-cpu.txt, not gated):
//   (a-sweep) at 192 kHz; a step-train arm (60 <-> 250 Hz every 50 ms) at 48 kHz;
//   (b) the inclusive increment over a baseline with no field at all (all-zero vector handed, no
//       query), i.e. spec SC-013(b)'s "law query plus pan forwarding" read literally; the handoff
//       count per query (proves the worst cadence: one changed vector per interval per voice);
//   (c) handoffs per second on a static note with MOTION off, after 2 s of settling (expected 0).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/profundum_core.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace Krate::DSP;

namespace {

constexpr double kSfpFs = 48000.0;
constexpr double kSfpFsHigh = 192000.0;
constexpr std::size_t kSfpBlock = 64;                                    // host block
constexpr std::size_t kSfpInterval = BassStereoField::kControlInterval;  // 32
constexpr int kSfpPartials = ProfundumCore::kDefaultPartials;            // 64
constexpr int kSfpReps = 5;                                              // best of 5
constexpr double kSfpStageSeconds = 60.0;                                // (a): 60 s of audio per repetition
constexpr double kSfpVoiceSeconds = 10.0;                                // (b): audio per repetition
constexpr double kSfpPickSeconds = 10.0;                                 // (a) source pick: one pass each
constexpr double kSfpSettleSeconds = 2.0;                                // (c)
constexpr double kSfpCountSeconds = 10.0;                                // (c)
constexpr double kSfpROffsetPeriods = 0.37;                              // R = L delayed by round(0.37 P)
constexpr double kSfpLoopPeriods = 103.0;                                // input loop length, E1 periods
constexpr double kSfpSweepLegSec = 0.1;                                  // triangle legs (a-sweep)
constexpr double kSfpStepSec = 0.05;                                     // step train (recorded)
constexpr float kSfpStaticCrossoverHz = 120.0f;
constexpr float kSfpE1Hz = 41.20344461410875f;
constexpr float kSfpE2Hz = 82.4068892282175f;
constexpr float kSfpC3Hz = 130.8127826502993f;
constexpr float kSfpA1Hz = 55.0f;
constexpr double kSfpBarStagePct = 0.5;   // SC-013(a), (a-sweep)
constexpr double kSfpBarVoicePct = 0.25;  // SC-013(b), one voice
constexpr double kSfpBarFourPct = 1.0;    // SC-013(b), four voices

static_assert(kSfpBlock % kSfpInterval == 0, "the host block holds whole control intervals");
static_assert(kSfpInterval == ProfundumCore::kControlInterval, "one grid for the field and the core");
static_assert(kSfpPartials == BassStereoField::kDefaultPartials, "one partial count for the field and the core");

enum class SfpCrossover : std::uint8_t { Static, Triangle, StepTrain };

const char* sfpSourceName(BassStereoField::MotionSource s) noexcept {
    return s == BassStereoField::MotionSource::Drift ? "drift" : "cycle";
}

const char* sfpCrossoverName(SfpCrossover x) noexcept {
    switch (x) {
        case SfpCrossover::Static: return "static";
        case SfpCrossover::Triangle: return "triangle";
        case SfpCrossover::StepTrain: return "steptrain";
    }
    return "?";
}

std::unique_ptr<ProfundumCore> sfpMakeCore(double fs, float f0) {
    auto core = std::make_unique<ProfundumCore>();
    core->prepare(fs, kSfpPartials);
    core->setControls(SpectralShapeRecipe::kSawAnchor);
    core->setFrequency(f0);
    core->noteOn(f0);
    return core;
}

BassStereoField::Controls sfpControls(float spread, float gravity, float depth, BassStereoField::MotionSource src) {
    BassStereoField::Controls c;
    c.mode = BassStereoField::ImageMode::Spread;
    c.spread = spread;
    c.gravity = gravity;
    c.crossoverHz = kSfpStaticCrossoverHz;
    c.curve = 0.5f;
    c.motionDepth = depth;
    c.motionRate = 1.0f;   // the fastest rate (SC-013(b) worst cadence)
    c.motionSource = src;
    return c;
}

/// The stage input loop: L = a kSawAnchor E1 ProfundumCore render (0.5 s settled), R = L delayed by
/// round(0.37 P) samples (cyclic over the loop, so the pair is distinct everywhere).
struct SfpInput {
    std::vector<float> left;
    std::vector<float> right;
    std::size_t shift = 0;
};

SfpInput sfpMakeInput(double fs) {
    const double period = fs / static_cast<double>(kSfpE1Hz);
    const auto loopLen = static_cast<std::size_t>(std::llround(kSfpLoopPeriods * period));
    SfpInput in;
    in.shift = static_cast<std::size_t>(std::llround(kSfpROffsetPeriods * period));
    in.left.assign(loopLen, 0.0f);
    in.right.assign(loopLen, 0.0f);
    auto core = sfpMakeCore(fs, kSfpE1Hz);
    std::array<float, kSfpBlock> wl{};
    std::array<float, kSfpBlock> wr{};
    const auto settleBlocks = static_cast<std::size_t>(fs * 0.5) / kSfpBlock;
    for (std::size_t b = 0; b < settleBlocks; ++b)
        core->processBlock(wl.data(), wr.data(), kSfpBlock);
    std::vector<float> scratchR(loopLen, 0.0f);
    for (std::size_t off = 0; off < loopLen; off += kSfpBlock) {
        const std::size_t run = std::min(kSfpBlock, loopLen - off);
        core->processBlock(in.left.data() + off, scratchR.data() + off, run);
    }
    for (std::size_t n = 0; n < loopLen; ++n)
        in.right[n] = in.left[(n + loopLen - in.shift) % loopLen];
    return in;
}

/// Times `block` (one host block per call): 1 s untimed warm-up, then `reps` repetitions of `seconds`
/// of audio. Returns min(wall) / audio x 100 (% of one core).
template <typename BlockFn>
double sfpTime(double fs, double seconds, int reps, const BlockFn& block) {
    const auto warmBlocks = static_cast<std::size_t>(fs) / kSfpBlock;
    for (std::size_t b = 0; b < warmBlocks; ++b)
        block();
    const auto blocksPerRep = static_cast<std::size_t>(std::llround(seconds * fs)) / kSfpBlock;
    const double audioSeconds = static_cast<double>(blocksPerRep * kSfpBlock) / fs;
    double best = std::numeric_limits<double>::infinity();
    for (int rep = 0; rep < reps; ++rep) {
        const auto t0 = std::chrono::steady_clock::now();
        for (std::size_t b = 0; b < blocksPerRep; ++b)
            block();
        const auto t1 = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double>(t1 - t0).count() / audioSeconds * 100.0);
    }
    return best;
}

/// The crossover control at interval `index` (sample rate fs).
float sfpCrossoverControl(SfpCrossover x, std::uint64_t index, double fs) noexcept {
    const double t = static_cast<double>(index) * static_cast<double>(kSfpInterval) / fs;
    const double lo = static_cast<double>(BassStereoField::kMinCrossoverHz);
    const double hi = static_cast<double>(BassStereoField::kMaxCrossoverHz);
    switch (x) {
        case SfpCrossover::Static: return kSfpStaticCrossoverHz;
        case SfpCrossover::Triangle: {
            const double p = std::fmod(t, 2.0 * kSfpSweepLegSec);
            const double tri = p < kSfpSweepLegSec ? p / kSfpSweepLegSec : 2.0 - p / kSfpSweepLegSec;
            return static_cast<float>(lo + (hi - lo) * tri);
        }
        case SfpCrossover::StepTrain: {
            const auto step = static_cast<std::uint64_t>(std::floor(t / kSfpStepSec));
            return static_cast<float>((step % 2u == 0u) ? lo : hi);
        }
    }
    return kSfpStaticCrossoverHz;
}

/// SC-013(a) / (a-sweep): the global field (advanceControl + safety stage) on the distinct pair.
double sfpMeasureStage(double fs, const SfpInput& in, BassStereoField::MotionSource src, SfpCrossover x,
                       double seconds, int reps, double& checksum) {
    auto field = std::make_unique<BassStereoField>();
    BassStereoField::Controls c = sfpControls(1.0f, 1.0f, 1.0f, src);
    c.crossoverHz = sfpCrossoverControl(x, 0, fs);
    field->setControls(c);
    field->prepare(fs, kSfpBlock);
    std::array<float, kSfpBlock> l{};
    std::array<float, kSfpBlock> r{};
    std::size_t pos = 0;
    std::uint64_t intervalIndex = 0;
    const std::size_t loopLen = in.left.size();

    const auto block = [&]() {
        for (std::size_t i = 0; i < kSfpBlock; ++i) {
            l[i] = in.left[pos];
            r[i] = in.right[pos];
            if (++pos >= loopLen)
                pos = 0;
        }
        for (std::size_t off = 0; off < kSfpBlock; off += kSfpInterval) {
            if (x != SfpCrossover::Static) {
                c.crossoverHz = sfpCrossoverControl(x, intervalIndex, fs);
                field->setControls(c);
            }
            ++intervalIndex;
            field->advanceControl();
            field->processBlock(l.data() + off, r.data() + off, kSfpInterval);
        }
        checksum += static_cast<double>(l[kSfpBlock - 1]) + static_cast<double>(r[kSfpBlock - 1]);
    };
    const double pct = sfpTime(fs, seconds, reps, block);
    checksum += static_cast<double>(field->motionValue()) + static_cast<double>(field->smoothedCrossoverHz());
    return pct;
}

enum class SfpVoiceCell : std::uint8_t {
    NoField,   // no field: an all-zero vector handed each interval (inclusive baseline, recorded)
    Spread0,   // the field at Spread 0: advance + query + handoff (the gated baseline)
    Spread1,   // the field at Spread 1, depth 1, rate 1 (the measured cell)
};

struct SfpVoiceResult {
    double pct = 0.0;
    std::uint64_t handoffs = 0;
    std::uint64_t queries = 0;
};

/// SC-013(b): `numVoices` cores rendered per interval into a stereo mix, with or without the field.
SfpVoiceResult sfpMeasureVoices(const float* f0s, std::size_t numVoices, SfpVoiceCell cell,
                                BassStereoField::MotionSource src, double& checksum) {
    std::vector<std::unique_ptr<ProfundumCore>> cores;
    for (std::size_t v = 0; v < numVoices; ++v)
        cores.push_back(sfpMakeCore(kSfpFs, f0s[v]));
    auto field = std::make_unique<BassStereoField>();
    const float spread = (cell == SfpVoiceCell::Spread1) ? 1.0f : 0.0f;
    field->setControls(sfpControls(spread, 0.5f, 1.0f, src));
    field->prepare(kSfpFs, kSfpBlock);
    std::vector<BassStereoField::VoiceState> states(numVoices);
    std::uint64_t queries = 0;
    std::array<float, kMaxPartials> pan{};
    const std::array<float, kMaxPartials> allZero{};
    std::array<float, kSfpBlock> mixL{};
    std::array<float, kSfpBlock> mixR{};
    std::array<float, kSfpBlock> vl{};
    std::array<float, kSfpBlock> vr{};

    const auto block = [&]() {
        mixL.fill(0.0f);
        mixR.fill(0.0f);
        for (std::size_t off = 0; off < kSfpBlock; off += kSfpInterval) {
            if (cell != SfpVoiceCell::NoField)
                field->advanceControl();   // once per interval, globally, before any voice (C-7)
            for (std::size_t v = 0; v < numVoices; ++v) {
                if (cell != SfpVoiceCell::NoField) {
                    field->computePanVector(states[v], f0s[v], pan, kSfpPartials);
                    ++queries;
                    cores[v]->setPartialPanOffsets(pan);
                } else {
                    cores[v]->setPartialPanOffsets(allZero);
                }
                cores[v]->processBlock(vl.data() + off, vr.data() + off, kSfpInterval);
                for (std::size_t i = off; i < off + kSfpInterval; ++i) {
                    mixL[i] += vl[i];
                    mixR[i] += vr[i];
                }
            }
        }
        checksum += static_cast<double>(mixL[kSfpBlock - 1]) + static_cast<double>(mixR[kSfpBlock - 1]);
    };
    SfpVoiceResult res;
    res.pct = sfpTime(kSfpFs, kSfpVoiceSeconds, kSfpReps, block);
    for (const auto& s : states)
        res.handoffs += s.handoffCount;
    res.queries = queries;
    checksum += static_cast<double>(pan[5]);
    return res;
}

/// SC-013(c): handoffs per second on a static E1 note, MOTION off, after kSfpSettleSeconds.
double sfpStaticHandoffsPerSecond() {
    auto field = std::make_unique<BassStereoField>();
    field->setControls(sfpControls(0.5f, 0.5f, 0.0f, BassStereoField::MotionSource::Drift));
    field->prepare(kSfpFs, kSfpBlock);
    BassStereoField::VoiceState state;
    std::array<float, kMaxPartials> pan{};
    const auto intervalsPerSecond = static_cast<std::size_t>(kSfpFs) / kSfpInterval;
    const auto settle = static_cast<std::size_t>(kSfpSettleSeconds * static_cast<double>(intervalsPerSecond));
    const auto count = static_cast<std::size_t>(kSfpCountSeconds * static_cast<double>(intervalsPerSecond));
    for (std::size_t i = 0; i < settle; ++i) {
        field->advanceControl();
        field->computePanVector(state, kSfpE1Hz, pan, kSfpPartials);
    }
    const std::uint64_t before = state.handoffCount;
    for (std::size_t i = 0; i < count; ++i) {
        field->advanceControl();
        field->computePanVector(state, kSfpE1Hz, pan, kSfpPartials);
    }
    return static_cast<double>(state.handoffCount - before) / kSfpCountSeconds;
}

std::string sfpEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase7-stereo-field/evidence/";
}

/// Prints a line and appends it to the evidence log.
void sfpEmit(std::ostringstream& log, const char* line) {
    std::printf("%s\n", line);
    std::fflush(stdout);
    log << line << '\n';
}

}  // namespace

// SC-013 (FR-079). Do NOT run concurrently with anything else; run pinned via evidence/close/cpu-lane.sh.
TEST_CASE("BassStereoField_Cpu", "[systems][profundum][stereo-field][sc-013][performance][.perf]") {
    double checksum = 0.0;
    std::ostringstream log;
    char line[512];

    const SfpInput in48 = sfpMakeInput(kSfpFs);
    std::snprintf(line, sizeof(line),
                  "SC013 setup fs=%.0f block=%zu interval=%zu partials=%d fixture=kSawAnchor stage_input=E1 "
                  "(%.5f Hz) loop=%zu samples R_delay=%zu samples (0.37 P) reps=best of %d warmup=1 s "
                  "stage_audio=%.0f s voice_audio=%.0f s",
                  kSfpFs, kSfpBlock, kSfpInterval, kSfpPartials, static_cast<double>(kSfpE1Hz), in48.left.size(),
                  in48.shift, kSfpReps, kSfpStageSeconds, kSfpVoiceSeconds);
    sfpEmit(log, line);

    // ---- (a) source pick: one timing pass of each source (best of 1), the dearer one is gated ----
    const std::array<BassStereoField::MotionSource, 2> sources{BassStereoField::MotionSource::Drift,
                                                      BassStereoField::MotionSource::Cycle};
    std::array<double, 2> pickPct{0.0, 0.0};
    for (std::size_t s = 0; s < sources.size(); ++s) {
        pickPct[s] = sfpMeasureStage(kSfpFs, in48, sources[s], SfpCrossover::Static, kSfpPickSeconds, 1, checksum);
        std::snprintf(line, sizeof(line), "SC013 PICK source=%s pct=%.4f (one pass of %.0f s)",
                      sfpSourceName(sources[s]), pickPct[s], kSfpPickSeconds);
        sfpEmit(log, line);
    }
    const BassStereoField::MotionSource dearer = (pickPct[1] > pickPct[0]) ? sources[1] : sources[0];
    std::snprintf(line, sizeof(line), "SC013 PICK dearer=%s", sfpSourceName(dearer));
    sfpEmit(log, line);

    // ---- (a), (a-sweep) gated at 48 kHz ----
    const double aPct = sfpMeasureStage(kSfpFs, in48, dearer, SfpCrossover::Static, kSfpStageSeconds, kSfpReps,
                                        checksum);
    std::snprintf(line, sizeof(line),
                  "SC013 A stage SPREAD spread=1 gravity=1 crossover=%s %.0f Hz motion=%s depth=1 rate=1 fs=%.0f "
                  "pct=%.4f bar=%.2f",
                  sfpCrossoverName(SfpCrossover::Static), static_cast<double>(kSfpStaticCrossoverHz),
                  sfpSourceName(dearer), kSfpFs, aPct, kSfpBarStagePct);
    sfpEmit(log, line);

    const double sweepPct = sfpMeasureStage(kSfpFs, in48, dearer, SfpCrossover::Triangle, kSfpStageSeconds,
                                            kSfpReps, checksum);
    std::snprintf(line, sizeof(line),
                  "SC013 A_SWEEP stage crossover=triangle 60<->250 Hz legs=%.0f ms motion=%s fs=%.0f pct=%.4f "
                  "bar=%.2f",
                  kSfpSweepLegSec * 1000.0, sfpSourceName(dearer), kSfpFs, sweepPct, kSfpBarStagePct);
    sfpEmit(log, line);

    // ---- recorded stage arms ----
    const double stepPct = sfpMeasureStage(kSfpFs, in48, dearer, SfpCrossover::StepTrain, kSfpStageSeconds,
                                           kSfpReps, checksum);
    std::snprintf(line, sizeof(line),
                  "SC013 RECORD step_train crossover=60<->250 Hz every %.0f ms motion=%s fs=%.0f pct=%.4f "
                  "(not gated)",
                  kSfpStepSec * 1000.0, sfpSourceName(dearer), kSfpFs, stepPct);
    sfpEmit(log, line);

    {
        const SfpInput in192 = sfpMakeInput(kSfpFsHigh);
        const double sweep192Pct = sfpMeasureStage(kSfpFsHigh, in192, dearer, SfpCrossover::Triangle,
                                                   kSfpStageSeconds, kSfpReps, checksum);
        std::snprintf(line, sizeof(line),
                      "SC013 RECORD a_sweep_192k crossover=triangle 60<->250 Hz legs=%.0f ms motion=%s fs=%.0f "
                      "pct=%.4f (not gated)",
                      kSfpSweepLegSec * 1000.0, sfpSourceName(dearer), kSfpFsHigh, sweep192Pct);
        sfpEmit(log, line);
    }

    // ---- (b) per voice, each source; gated = the worse source ----
    const std::array<float, 4> f0s{kSfpE1Hz, kSfpE2Hz, kSfpC3Hz, kSfpA1Hz};
    double b1 = 0.0;
    double b4 = 0.0;
    for (const std::size_t voices : {std::size_t{1}, std::size_t{4}}) {
        const SfpVoiceResult noField =
            sfpMeasureVoices(f0s.data(), voices, SfpVoiceCell::NoField, BassStereoField::MotionSource::Drift,
                             checksum);
        std::snprintf(line, sizeof(line), "SC013 B voices=%zu cell=no_field pct=%.4f", voices, noField.pct);
        sfpEmit(log, line);
        for (const BassStereoField::MotionSource src : sources) {
            const SfpVoiceResult base = sfpMeasureVoices(f0s.data(), voices, SfpVoiceCell::Spread0, src, checksum);
            const SfpVoiceResult law = sfpMeasureVoices(f0s.data(), voices, SfpVoiceCell::Spread1, src, checksum);
            const double inc = law.pct - base.pct;
            const double incl = law.pct - noField.pct;
            std::snprintf(line, sizeof(line),
                          "SC013 B voices=%zu source=%s spread0_pct=%.4f spread1_pct=%.4f increment=%.4f "
                          "inclusive_increment=%.4f (not gated) handoffs=%llu queries=%llu",
                          voices, sfpSourceName(src), base.pct, law.pct, inc, incl,
                          static_cast<unsigned long long>(law.handoffs), static_cast<unsigned long long>(law.queries));
            sfpEmit(log, line);
            if (voices == 1)
                b1 = std::max(b1, inc);
            else
                b4 = std::max(b4, inc);
        }
    }
    std::snprintf(line, sizeof(line), "SC013 B worst one_voice=%.4f bar=%.2f four_voices=%.4f bar=%.2f", b1,
                  kSfpBarVoicePct, b4, kSfpBarFourPct);
    sfpEmit(log, line);

    // ---- (c) recorded ----
    const double handoffsPerSec = sfpStaticHandoffsPerSecond();
    std::snprintf(line, sizeof(line),
                  "SC013 RECORD c handoffs_per_second=%.3f static E1, MOTION off, after %.0f s settling "
                  "(expected 0; not gated)",
                  handoffsPerSec, kSfpSettleSeconds);
    sfpEmit(log, line);

    const bool aOk = aPct <= kSfpBarStagePct;
    const bool sweepOk = sweepPct <= kSfpBarStagePct;
    const bool b1Ok = b1 <= kSfpBarVoicePct;
    const bool b4Ok = b4 <= kSfpBarFourPct;
    const bool pass = aOk && sweepOk && b1Ok && b4Ok;
    std::snprintf(line, sizeof(line), "SC013 VERDICT %s %.4f %.4f %.4f %.4f", pass ? "PASS" : "FAIL", aPct,
                  sweepPct, b1, b4);
    sfpEmit(log, line);
    std::snprintf(line, sizeof(line),
                  "SC013 GATED a=%.4f<=%.2f(%s) a_sweep=%.4f<=%.2f(%s) b1=%.4f<=%.2f(%s) b4=%.4f<=%.2f(%s)", aPct,
                  kSfpBarStagePct, aOk ? "PASS" : "FAIL", sweepPct, kSfpBarStagePct, sweepOk ? "PASS" : "FAIL", b1,
                  kSfpBarVoicePct, b1Ok ? "PASS" : "FAIL", b4, kSfpBarFourPct, b4Ok ? "PASS" : "FAIL");
    sfpEmit(log, line);

    const std::string dir = sfpEvidenceDir();
    std::filesystem::create_directories(dir);
    std::ofstream out(dir + "sc013-cpu.txt");
    REQUIRE(out.is_open());
    out << log.str();
    out.close();

    REQUIRE(detail::isFinite(checksum));   // keeps the renders from being elided
    CHECK(aPct <= kSfpBarStagePct);       // SC-013(a): <= 0.5 % of one core
    CHECK(sweepPct <= kSfpBarStagePct);   // SC-013(a-sweep): <= 0.5 % at 48 kHz
    CHECK(b1 <= kSfpBarVoicePct);         // SC-013(b): <= 0.25 % for one voice
    CHECK(b4 <= kSfpBarFourPct);          // SC-013(b): <= 1.0 % for four voices
}
