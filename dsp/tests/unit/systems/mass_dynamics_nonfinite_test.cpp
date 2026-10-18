// ==============================================================================
// Layer 3: System Tests - BassCompressor, MassDynamics, MassLaw, non-finite TU
//                          (Profundum Phase 6, specs/profundum-phase6-mass-dynamics)
// ==============================================================================
// Reference: specs/profundum-phase6-mass-dynamics/spec.md (SC-019; FR-017, FR-018, FR-029)
//            specs/profundum-phase6-mass-dynamics/tasks.md (T002 wires this TU, T017 fills it)
//
// SCOPE (tags [systems][profundum][mass-dynamics][nonfinite]):
//   MassDynamics_NonFinite   SC-019 compressor, MassDynamics and MassLaw arms
//
// Listed in the -fno-fast-math block of dsp/tests/CMakeLists.txt: it injects NaN / +-Inf built
// from bit patterns (std::bit_cast, as T008). Output finiteness is read only through
// TransientShaperTest::isFiniteBits; value preservation is checked bitwise.
// TU-local names carry an `mdnf` / `kMdnf` prefix (tasks.md rule 5).
// ==============================================================================

#include "../processors/transient_shaper_test_helpers.h"

#include <krate/dsp/processors/transient_shaper.h>
#include <krate/dsp/systems/mass_dynamics.h>
#include <krate/dsp/systems/mass_law.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <vector>

using namespace Krate::DSP;
namespace mdnft = Krate::DSP::TransientShaperTest;

namespace {

constexpr double kMdnfFs = 48000.0;
constexpr std::size_t kMdnfHostBlock = 512;
constexpr std::size_t kMdnfBurstLen = 16;
constexpr std::size_t kMdnfStateCheckAfter = 512;  // "state finite within 1 block after the burst"
constexpr std::size_t kMdnfNumBursts = 6;

constexpr std::array<std::uint32_t, 3> kMdnfBadBits{0x7FC00000u, 0x7F800000u, 0xFF800000u};  // NaN, +Inf, -Inf
constexpr std::array<const char*, 3> kMdnfBadNames{"NaN", "+Inf", "-Inf"};

float mdnfFromBits(std::uint32_t bits) noexcept { return std::bit_cast<float>(bits); }
std::uint32_t mdnfBits(float x) noexcept { return std::bit_cast<std::uint32_t>(x); }

struct MdnfBurst {
    std::size_t start;
    std::size_t patternIndex;  // into kMdnfBadBits
    bool bothChannels;
};

/// Renders L / R in place in host blocks of at most kMdnfHostBlock; a block also ends at every
/// checkpoint (sorted, strictly increasing), where stateFinite() is recorded into `stateOk`.
template <typename Proc>
void mdnfRender(Proc& proc, std::vector<float>& L, std::vector<float>& R, const std::vector<std::size_t>& checkpoints,
                std::vector<bool>& stateOk) {
    REQUIRE(L.size() == R.size());
    stateOk.assign(checkpoints.size(), false);
    const std::size_t total = L.size();
    std::size_t pos = 0;
    std::size_t ci = 0;
    while (pos < total) {
        std::size_t end = std::min(total, pos + kMdnfHostBlock);
        if (ci < checkpoints.size())
            end = std::min(end, checkpoints[ci]);
        proc.processBlock(L.data() + pos, R.data() + pos, end - pos);
        pos = end;
        while (ci < checkpoints.size() && checkpoints[ci] == pos) {
            stateOk[ci] = proc.stateFinite();
            ++ci;
        }
    }
    REQUIRE(ci == checkpoints.size());
}

/// Renders `seconds` of stereo silence in host blocks (crosses many 32-sample control boundaries).
template <typename Proc>
void mdnfRenderSilence(Proc& proc, double seconds) {
    std::vector<float> L(mdnft::secondsToSamples(kMdnfFs, seconds), 0.0f);
    std::vector<float> R = L;
    for (std::size_t pos = 0; pos < L.size(); pos += kMdnfHostBlock) {
        const std::size_t n = std::min(kMdnfHostBlock, L.size() - pos);
        proc.processBlock(L.data() + pos, R.data() + pos, n);
    }
}

/// The input-burst arm for one processor (configured and prepared by the caller).
/// spacingS: seconds between burst onsets; recoveryS: where L's one-period level is gated against the
/// pre-burst window (SC-019: 0.1 s for a single stage; 0.6 s = 100 ms past the longest D-5 release for the
/// whole chain, user ruling 2026-10-17, Clarifications "Group 7 gate rulings"). The 100 ms reading is
/// always printed.
template <typename Proc>
void mdnfBurstArm(Proc& proc, const char* label, double spacingS, double recoveryS) {
    const double fs = kMdnfFs;
    const double f0 = mdnft::kE1Hz;

    // Bursts k = 0..5 at 1.0 + spacingS k s: NaN, +Inf, -Inf on L only (k < 3), then on both channels.
    std::vector<MdnfBurst> bursts;
    for (std::size_t k = 0; k < kMdnfNumBursts; ++k)
        bursts.push_back(MdnfBurst{.start = mdnft::secondsToSamples(fs, 1.0 + spacingS * static_cast<double>(k)),
                                   .patternIndex = k % 3,
                                   .bothChannels = k >= 3});
    const double seconds = 1.0 + spacingS * static_cast<double>(kMdnfNumBursts);  // one spacing after the last burst

    // kSawAnchor E1 at -12 dBFS RMS, L == R (tasks.md rule 9).
    const std::vector<float> x = mdnft::steadyTone(mdnft::Tone::Saw, f0, fs, seconds);
    std::vector<float> inL = x;
    std::vector<float> inR = x;

    // Each burst is snapped to the saw's positive peak within the period after its nominal onset: the
    // worst-case dropout. The SC-019 ruling (2026-10-17) was taken on burst 3 landing on that peak at
    // the old 0.5 s spacing; the snap keeps the premise at any spacing, for both arms.
    const auto periodLen = static_cast<std::size_t>(std::ceil(fs / f0));
    for (MdnfBurst& b : bursts) {
        std::size_t best = b.start;
        for (std::size_t i = b.start; i < b.start + periodLen; ++i)
            if (x[i] > x[best])
                best = i;
        b.start = best;
    }
    for (const MdnfBurst& b : bursts) {
        REQUIRE(b.start + kMdnfBurstLen + kMdnfStateCheckAfter <= x.size());
        const float bad = mdnfFromBits(kMdnfBadBits[b.patternIndex]);
        for (std::size_t i = 0; i < kMdnfBurstLen; ++i) {
            inL[b.start + i] = bad;
            if (b.bothChannels)
                inR[b.start + i] = bad;
        }
    }

    std::vector<std::size_t> checkpoints;
    for (const MdnfBurst& b : bursts)
        checkpoints.push_back(b.start + kMdnfBurstLen + kMdnfStateCheckAfter);

    std::vector<float> outL = inL;
    std::vector<float> outR = inR;
    std::vector<bool> stateOk;
    mdnfRender(proc, outL, outR, checkpoints, stateOk);

    // Every output sample finite (bit pattern); each sample whose input was non-finite is exactly 0.0f.
    std::size_t nonFiniteOut = 0;
    std::size_t sanitisedNotZero = 0;
    std::size_t sanitisedCount = 0;
    for (std::size_t n = 0; n < outL.size(); ++n) {
        if (!mdnft::isFiniteBits(outL[n]))
            ++nonFiniteOut;
        if (!mdnft::isFiniteBits(outR[n]))
            ++nonFiniteOut;
        if (!mdnft::isFiniteBits(inL[n])) {
            ++sanitisedCount;
            if (outL[n] != 0.0f)
                ++sanitisedNotZero;
        }
        if (!mdnft::isFiniteBits(inR[n])) {
            ++sanitisedCount;
            if (outR[n] != 0.0f)
                ++sanitisedNotZero;
        }
    }
    std::printf("MassDynamics_NonFinite [%s]: non-finite outputs %zu, sanitised input samples %zu, of which "
                "non-zero at the output %zu\n",
                label, nonFiniteOut, sanitisedCount, sanitisedNotZero);
    CHECK(sanitisedCount == kMdnfBurstLen * 9);  // 3 L-only bursts + 3 two-channel bursts
    CHECK(nonFiniteOut == 0);
    CHECK(sanitisedNotZero == 0);

    // One E1 period of L, recoveryS after each burst ends, vs the same-length window ending at the
    // burst onset (T008 D-1); the 100 ms reading is printed as well.
    const std::size_t after = mdnft::secondsToSamples(fs, recoveryS);
    const std::size_t after100 = mdnft::secondsToSamples(fs, 0.1);
    for (std::size_t k = 0; k < bursts.size(); ++k) {
        const MdnfBurst& b = bursts[k];
        INFO(label << " burst " << k << " " << kMdnfBadNames[b.patternIndex] << (b.bothChannels ? " L+R" : " L only"));
        const bool stateFiniteAfter = stateOk[k];
        CHECK(stateFiniteAfter);

        REQUIRE(b.start >= periodLen);
        const std::size_t postStart = b.start + kMdnfBurstLen + after;
        REQUIRE(postStart + periodLen <= outL.size());
        const double preDb = mdnft::levelDbPeriods(outL.data() + (b.start - periodLen), periodLen, fs, f0);
        const double postDb = mdnft::levelDbPeriods(outL.data() + postStart, periodLen, fs, f0);
        const double post100Db =
            mdnft::levelDbPeriods(outL.data() + (b.start + kMdnfBurstLen + after100), periodLen, fs, f0);
        const double delta = postDb - preDb;
        std::printf("MassDynamics_NonFinite [%s]: burst %zu %s %s pre %.6f dB post(%.0f ms) %.6f dB delta %.6f dB "
                    "(at 100 ms %.6f dB, recorded) stateFinite %d\n",
                    label, k, kMdnfBadNames[b.patternIndex], b.bothChannels ? "L+R" : "L", preDb, recoveryS * 1000.0,
                    postDb, delta, post100Db - preDb, stateFiniteAfter ? 1 : 0);
        INFO("pre " << preDb << " dB, post " << postDb << " dB");
        CHECK(std::abs(delta) <= 0.1);
    }
}

/// MassLaw::Targets field by index, in MassLaw::Target order (TU-local; the law's accessor is private).
float& mdnfField(MassLaw::Targets& t, std::size_t i) noexcept {
    switch (i) {
        case 0: return t.depth;
        case 1: return t.body;
        case 2: return t.transientDurationMs;
        case 3: return t.foldAmount;
        case 4: return t.compressionAmount;
        case 5: return t.anchorLevel;
        default: return t.outputTrimDb;
    }
}
constexpr std::array<const char*, MassLaw::kNumTargets> kMdnfFieldNames{
    "depth", "body", "transientDurationMs", "foldAmount", "compressionAmount", "anchorLevel", "outputTrimDb"};

static_assert(sizeof(MassLaw::Targets) == MassLaw::kNumTargets * sizeof(float), "MassLaw::Targets has no padding");

bool mdnfTargetsBitEqual(const MassLaw::Targets& a, const MassLaw::Targets& b) noexcept {
    const auto eq = [](float x, float y) { return std::bit_cast<std::uint32_t>(x) == std::bit_cast<std::uint32_t>(y); };
    return eq(a.depth, b.depth) && eq(a.body, b.body) && eq(a.transientDurationMs, b.transientDurationMs) &&
           eq(a.foldAmount, b.foldAmount) && eq(a.compressionAmount, b.compressionAmount) &&
           eq(a.anchorLevel, b.anchorLevel) && eq(a.outputTrimDb, b.outputTrimDb);
}

/// A non-default, in-range base for every field.
MassLaw::Targets mdnfBase() noexcept {
    MassLaw::Targets b{};
    b.depth = 0.3f;
    b.body = 0.7f;
    b.transientDurationMs = 100.0f;
    b.foldAmount = 20.0f;
    b.compressionAmount = 0.4f;
    b.anchorLevel = 0.6f;
    return b;
}

}  // namespace

// SC-019 compressor, MassDynamics and MassLaw arms (FR-017, FR-018, FR-029): 16-sample NaN / +Inf /
// -Inf bursts on L only, then on both channels, after 1 s steady; every output finite,
// the sanitised samples exactly 0.0f, state finite 512 samples after each burst, L's one-period level
// within 0.1 dB of the pre-burst window 100 ms after each burst (compressor arm, bursts 0.5 s apart) or
// 600 ms after it (whole chain, bursts 1 s apart; SC-019 ruling 2026-10-17); every burst snapped to
// the saw's positive peak (the worst-case dropout); non-finite setters keep the previous
// value (the detector key goes to "no voice", FR-018); MassLaw's non-finite Mass / base handling.
TEST_CASE("MassDynamics_NonFinite", "[systems][profundum][mass-dynamics][nonfinite]") {
    const auto keyE1 = static_cast<float>(mdnft::kE1Hz);

    SECTION("BassCompressor input bursts") {
        BassCompressor comp;
        comp.setAmount(1.0f);
        comp.setDetectorKeyHz(keyE1);
        comp.prepare(kMdnfFs, kMdnfHostBlock);
        CHECK(comp.amount() == 1.0f);
        mdnfBurstArm(comp, "BassCompressor", 0.5, 0.1);
    }

    SECTION("MassDynamics input bursts") {
        MassDynamics md;
        md.setTransientAttackDb(12.0f);
        md.setTransientSustainDb(-6.0f);
        md.setCompressionAmount(1.0f);
        md.setDetectorKeyHz(keyE1);
        md.prepare(kMdnfFs, kMdnfHostBlock);
        CHECK(md.shaper().attackGainDb() == 12.0f);
        CHECK(md.shaper().sustainGainDb() == -6.0f);
        CHECK(md.compressor().amount() == 1.0f);
        // Whole chain: 600 ms = 100 ms past the longest D-5 release, bursts 1 s apart (SC-019 ruling 2026-10-17).
        mdnfBurstArm(md, "MassDynamics", 1.0, 0.6);
    }

    // Key 200 Hz -> corner 400 Hz, far from the 80 Hz "no voice" corner (E1's 82.4 Hz corner is too
    // close to 80 Hz to tell "fallback" from "kept the previous key").
    constexpr float kKeyHz = 200.0f;
    constexpr float kKeyCornerHz = 400.0f;
    constexpr float kCornerTolHz = 0.01f;

    SECTION("BassCompressor non-finite setters") {
        for (const bool prepared : {false, true}) {
            for (std::size_t p = 0; p < kMdnfBadBits.size(); ++p) {
                INFO((prepared ? "after prepare, " : "before prepare, ") << kMdnfBadNames[p]);
                const float bad = mdnfFromBits(kMdnfBadBits[p]);
                BassCompressor comp;
                comp.setAmount(0.5f);
                comp.setDetectorKeyHz(kKeyHz);
                if (prepared) {
                    comp.prepare(kMdnfFs, kMdnfHostBlock);
                    mdnfRenderSilence(comp, 0.5);
                    CHECK(comp.amount() == 0.5f);
                    CHECK(std::abs(comp.detectorCornerHz() - kKeyCornerHz) <= kCornerTolHz);
                }
                comp.setAmount(bad);
                comp.setDetectorKeyHz(bad);
                if (!prepared)
                    comp.prepare(kMdnfFs, kMdnfHostBlock);
                mdnfRenderSilence(comp, 0.5);
                std::printf("MassDynamics_NonFinite [BassCompressor setters] %s %s: amount %.6f corner %.6f Hz\n",
                            prepared ? "after prepare" : "before prepare", kMdnfBadNames[p],
                            static_cast<double>(comp.amount()), static_cast<double>(comp.detectorCornerHz()));
                CHECK(mdnfBits(comp.amount()) == mdnfBits(0.5f));
                // FR-018: a non-finite key is "no voice" -> the 80 Hz corner, not the previous key.
                CHECK(std::abs(comp.detectorCornerHz() - BassCompressor::kFallbackCornerHz) <= kCornerTolHz);
                CHECK(comp.stateFinite());
            }
        }
    }

    SECTION("MassDynamics non-finite setters") {
        for (const bool prepared : {false, true}) {
            for (std::size_t p = 0; p < kMdnfBadBits.size(); ++p) {
                INFO((prepared ? "after prepare, " : "before prepare, ") << kMdnfBadNames[p]);
                const float bad = mdnfFromBits(kMdnfBadBits[p]);
                MassDynamics md;
                md.setTransientDurationMs(100.0f);
                md.setTransientAttackDb(12.0f);
                md.setTransientSustainDb(-6.0f);
                md.setCompressionAmount(0.5f);
                md.setDetectorKeyHz(kKeyHz);
                if (prepared) {
                    md.prepare(kMdnfFs, kMdnfHostBlock);
                    mdnfRenderSilence(md, 0.5);
                    CHECK(md.shaper().transientDurationMs() == 100.0f);
                    CHECK(md.shaper().attackGainDb() == 12.0f);
                    CHECK(md.shaper().sustainGainDb() == -6.0f);
                    CHECK(md.compressor().amount() == 0.5f);
                    CHECK(std::abs(md.compressor().detectorCornerHz() - kKeyCornerHz) <= kCornerTolHz);
                }
                md.setTransientDurationMs(bad);
                md.setTransientAttackDb(bad);
                md.setTransientSustainDb(bad);
                md.setCompressionAmount(bad);
                md.setDetectorKeyHz(bad);
                if (!prepared)
                    md.prepare(kMdnfFs, kMdnfHostBlock);
                mdnfRenderSilence(md, 0.5);
                std::printf("MassDynamics_NonFinite [MassDynamics setters] %s %s: duration %.6f ms attack %.6f dB "
                            "sustain %.6f dB amount %.6f corner %.6f Hz\n",
                            prepared ? "after prepare" : "before prepare", kMdnfBadNames[p],
                            static_cast<double>(md.shaper().transientDurationMs()),
                            static_cast<double>(md.shaper().attackGainDb()),
                            static_cast<double>(md.shaper().sustainGainDb()),
                            static_cast<double>(md.compressor().amount()),
                            static_cast<double>(md.compressor().detectorCornerHz()));
                CHECK(mdnfBits(md.shaper().transientDurationMs()) == mdnfBits(100.0f));
                CHECK(mdnfBits(md.shaper().attackGainDb()) == mdnfBits(12.0f));
                CHECK(mdnfBits(md.shaper().sustainGainDb()) == mdnfBits(-6.0f));
                CHECK(mdnfBits(md.compressor().amount()) == mdnfBits(0.5f));
                CHECK(std::abs(md.compressor().detectorCornerHz() - BassCompressor::kFallbackCornerHz) <=
                      kCornerTolHz);
                CHECK(md.stateFinite());
            }
        }
    }

    SECTION("MassLaw non-finite Mass and base") {
        for (const bool prepared : {false, true}) {
            for (std::size_t p = 0; p < kMdnfBadBits.size(); ++p) {
                INFO((prepared ? "after prepare, " : "before prepare, ") << kMdnfBadNames[p]);
                const float bad = mdnfFromBits(kMdnfBadBits[p]);
                MassLaw law;
                if (prepared)
                    law.prepare(kMdnfFs);
                law.setMass(70.0f);
                law.setBase(mdnfBase());
                REQUIRE(law.pendingMass() == 70.0f);
                const MassLaw::Targets base0 = law.base();
                REQUIRE(mdnfTargetsBitEqual(base0, mdnfBase()));

                // setMass: non-finite keeps the previous pending value; the slew stays finite.
                law.setMass(bad);
                CHECK(mdnfBits(law.pendingMass()) == mdnfBits(70.0f));
                for (int i = 0; i < 100; ++i)
                    (void)law.advanceInterval();
                CHECK(mdnft::isFiniteBits(law.smoothedMass()));
                if (prepared) {
                    // 100 intervals = 66.7 ms at 48 kHz, past the 8 ms 50 -> 70 slew.
                    CHECK(law.smoothedMass() == 70.0f);
                }

                // setBase: one field non-finite keeps that field's previous base; the other fields
                // (equal to the previous ones) are unchanged.
                for (std::size_t f = 0; f < MassLaw::kNumTargets; ++f) {
                    INFO("setBase field " << kMdnfFieldNames[f]);
                    MassLaw::Targets b = base0;
                    mdnfField(b, f) = bad;
                    law.setBase(b);
                    CHECK(mdnfTargetsBitEqual(law.base(), base0));
                }
                // Every field at once.
                MassLaw::Targets allBad{};
                for (std::size_t f = 0; f < MassLaw::kNumTargets; ++f)
                    mdnfField(allBad, f) = bad;
                law.setBase(allBad);
                CHECK(mdnfTargetsBitEqual(law.base(), base0));

                // The current targets are finite.
                MassLaw::Targets cur = law.currentTargets();
                for (std::size_t f = 0; f < MassLaw::kNumTargets; ++f)
                    CHECK(mdnft::isFiniteBits(mdnfField(cur, f)));
            }
        }

        // evaluate: a non-finite Mass is the neutral Mass (the pure function has no "previous").
        for (std::size_t p = 0; p < kMdnfBadBits.size(); ++p) {
            INFO("evaluate mass " << kMdnfBadNames[p]);
            const float bad = mdnfFromBits(kMdnfBadBits[p]);
            for (const MassLaw::Targets& b : {MassLaw::Targets{}, mdnfBase()}) {
                const MassLaw::Targets got = MassLaw::evaluate(bad, b);
                const MassLaw::Targets want = MassLaw::evaluate(MassLaw::kNeutralMass, b);
                CHECK(mdnfTargetsBitEqual(got, want));
            }
        }

        // evaluate: a non-finite base field uses that field's Targets{} default.
        for (std::size_t p = 0; p < kMdnfBadBits.size(); ++p) {
            const float bad = mdnfFromBits(kMdnfBadBits[p]);
            for (std::size_t f = 0; f < MassLaw::kNumTargets; ++f) {
                MassLaw::Targets bBad = mdnfBase();
                mdnfField(bBad, f) = bad;
                MassLaw::Targets defaults{};
                MassLaw::Targets bDefault = mdnfBase();
                mdnfField(bDefault, f) = mdnfField(defaults, f);
                for (const float mass : {0.0f, 25.0f, 50.0f, 75.0f, 100.0f}) {
                    INFO("evaluate base field " << kMdnfFieldNames[f] << " = " << kMdnfBadNames[p] << ", mass "
                                                << mass);
                    MassLaw::Targets got = MassLaw::evaluate(mass, bBad);
                    const MassLaw::Targets want = MassLaw::evaluate(mass, bDefault);
                    CHECK(mdnfTargetsBitEqual(got, want));
                    for (std::size_t q = 0; q < MassLaw::kNumTargets; ++q)
                        CHECK(mdnft::isFiniteBits(mdnfField(got, q)));
                }
            }
        }
    }
}
