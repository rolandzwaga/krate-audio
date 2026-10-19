// ==============================================================================
// Layer 3: System Tests - BassStereoField, non-finite TU
//                          (Profundum Phase 7, specs/profundum-phase7-stereo-field)
// ==============================================================================
// Reference: specs/profundum-phase7-stereo-field/spec.md (SC-014, FR-072; listed in the -fno-fast-math block of dsp/tests/CMakeLists.txt)
//            specs/profundum-phase7-stereo-field/tasks.md (T002 wires this TU, T008 fills it)
//
// SCOPE (tags [systems][profundum][stereo-field][nonfinite][sc-014]):
//   BassStereoField_NonFinite          16-sample NaN / +Inf / -Inf bursts into the safety stage on L, R and both
//   BassStereoField_NonFiniteControls  NaN / +-Inf in every float Controls field and in setTempoBpm
//
// Non-finite values are built from bit patterns (std::bit_cast through a volatile), never from
// std::numeric_limits or arithmetic; finiteness is read only through detail::isFinite and value
// preservation is checked bitwise. Field-only renders (no chain): advanceControl() at every multiple
// of kControlInterval, before that interval's processBlock (plan S1 C-7). TU-local names carry a
// `bnf` / `kBnf` prefix.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/systems/bass_stereo_field.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

namespace {

using Krate::DSP::BassStereoField;
using BnfControls = Krate::DSP::BassStereoField::Controls;
using BnfMode = Krate::DSP::BassStereoField::ImageMode;
using BnfSource = Krate::DSP::BassStereoField::MotionSource;
using Krate::DSP::NoteModifier;
using Krate::DSP::NoteValue;
using Krate::DSP::detail::isFinite;

constexpr double kBnfFs = 48000.0;
constexpr std::size_t kBnfHostBlock = 64;
constexpr std::size_t kBnfTotal = 96000;          // 2 s at 48 kHz
constexpr std::size_t kBnfBurstStart = 24000;     // 0.5 s
constexpr std::size_t kBnfBurstLen = 16;
constexpr std::size_t kBnfBurstEnd = kBnfBurstStart + kBnfBurstLen;
constexpr std::size_t kBnfRecoveryStart = kBnfBurstEnd + 4800;   // 100 ms after the burst's end
constexpr std::size_t kBnfRecoveryLen = 24000;                   // the next 0.5 s
constexpr double kBnfRecoveryBoundDb = -60.0;

static_assert(kBnfRecoveryStart + kBnfRecoveryLen <= kBnfTotal);
static_assert(kBnfHostBlock % BassStereoField::kControlInterval == 0);

constexpr std::array<std::uint32_t, 3> kBnfBadBits{0x7FC00000u, 0x7F800000u, 0xFF800000u};  // NaN, +Inf, -Inf
constexpr std::array<const char*, 3> kBnfBadNames{"NaN", "+Inf", "-Inf"};

/// A non-finite float from its bit pattern, through a volatile so no fast-math folding can see it.
float bnfFromBits(std::uint32_t bits) noexcept {
    volatile std::uint32_t v = bits;
    return std::bit_cast<float>(static_cast<std::uint32_t>(v));
}

std::uint32_t bnfBits(float x) noexcept { return std::bit_cast<std::uint32_t>(x); }

/// Seeded independent uniform [-0.5, 0.5] noise on L and R.
void bnfNoise(std::vector<float>& L, std::vector<float>& R, std::size_t n) {
    std::mt19937 rng{0x5EEDu};
    std::uniform_real_distribution<float> d(-0.5f, 0.5f);
    L.assign(n, 0.0f);
    R.assign(n, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        L[i] = d(rng);
        R[i] = d(rng);
    }
}

/// Host blocks of kBnfHostBlock, each split at the 32-sample grid; advanceControl() at every grid point.
void bnfRender(BassStereoField& f, std::vector<float>& L, std::vector<float>& R) {
    REQUIRE(L.size() == R.size());
    const std::size_t n = L.size();
    for (std::size_t b = 0; b < n; b += kBnfHostBlock) {
        const std::size_t end = std::min(n, b + kBnfHostBlock);
        std::size_t pos = b;
        while (pos < end) {
            const std::size_t next = std::min(end, (pos / BassStereoField::kControlInterval + 1) *
                                                       BassStereoField::kControlInterval);
            if (pos % BassStereoField::kControlInterval == 0) {
                f.advanceControl();
            }
            f.processBlock(L.data() + pos, R.data() + pos, next - pos);
            pos = next;
        }
    }
}

/// SPREAD, Gravity 0.5, crossover 120 Hz (tasks.md T008), prepared at 48 kHz.
std::unique_ptr<BassStereoField> bnfMakeBurstField() {
    auto f = std::make_unique<BassStereoField>();
    BnfControls c{};
    c.mode = BnfMode::Spread;
    c.gravity = 0.5f;
    c.crossoverHz = 120.0f;
    f->setControls(c);
    f->prepare(kBnfFs, kBnfHostBlock);
    return f;
}

double bnfRms(const float* x, std::size_t n) {
    double e = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        e += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    }
    return std::sqrt(e / static_cast<double>(n));
}

/// RMS of (y - ref) over [start, start + n) re the reference's RMS, in dB (floored at -300 dB).
double bnfDiffDb(const std::vector<float>& y, const std::vector<float>& ref, std::size_t start, std::size_t n) {
    double e = 0.0;
    for (std::size_t i = start; i < start + n; ++i) {
        const double d = static_cast<double>(y[i]) - static_cast<double>(ref[i]);
        e += d * d;
    }
    const double diffRms = std::sqrt(e / static_cast<double>(n));
    const double refRms = bnfRms(ref.data() + start, n);
    REQUIRE(refRms > 0.0);
    return 20.0 * std::log10(std::max(diffRms / refRms, 1e-15));
}

/// Bitwise equality of two Controls (floats by bit pattern, enums / bool by value).
bool bnfControlsBitEqual(const BnfControls& a, const BnfControls& b) noexcept {
    return a.mode == b.mode && bnfBits(a.spread) == bnfBits(b.spread) && bnfBits(a.gravity) == bnfBits(b.gravity) &&
           bnfBits(a.crossoverHz) == bnfBits(b.crossoverHz) && bnfBits(a.curve) == bnfBits(b.curve) &&
           bnfBits(a.motionDepth) == bnfBits(b.motionDepth) && bnfBits(a.motionRate) == bnfBits(b.motionRate) &&
           a.motionSource == b.motionSource && a.cycleSync == b.cycleSync && a.cycleNote == b.cycleNote &&
           a.cycleModifier == b.cycleModifier;
}

}  // namespace

// ==============================================================================
// SC-014 / FR-072: a 16-sample non-finite burst into the safety stage
// ==============================================================================

TEST_CASE("BassStereoField_NonFinite", "[systems][profundum][stereo-field][nonfinite][sc-014][fr-072]") {
    std::vector<float> inL;
    std::vector<float> inR;
    bnfNoise(inL, inR, kBnfTotal);

    // The reference: the same noise, no burst.
    std::vector<float> refL = inL;
    std::vector<float> refR = inR;
    {
        auto f = bnfMakeBurstField();
        bnfRender(*f, refL, refR);
        REQUIRE_FALSE(f->sideIdle());   // non-vacuity: the side path runs in SPREAD
    }
    std::size_t refNonFinite = 0;
    for (std::size_t i = 0; i < kBnfTotal; ++i) {
        if (!isFinite(refL[i]) || !isFinite(refR[i])) {
            ++refNonFinite;
        }
    }
    REQUIRE(refNonFinite == 0);
    REQUIRE(bnfRms(refL.data() + kBnfRecoveryStart, kBnfRecoveryLen) > 0.0);
    REQUIRE(bnfRms(refR.data() + kBnfRecoveryStart, kBnfRecoveryLen) > 0.0);

    struct Arm {
        const char* name;
        bool badL;
        bool badR;
    };
    constexpr std::array<Arm, 3> kArms{Arm{"L only", true, false}, Arm{"R only", false, true},
                                       Arm{"both", true, true}};

    for (const Arm& arm : kArms) {
        for (std::size_t p = 0; p < kBnfBadBits.size(); ++p) {
            INFO("arm " << arm.name << ", burst " << kBnfBadNames[p]);
            const float bad = bnfFromBits(kBnfBadBits[p]);
            REQUIRE_FALSE(isFinite(bad));

            std::vector<float> L = inL;
            std::vector<float> R = inR;
            for (std::size_t i = kBnfBurstStart; i < kBnfBurstEnd; ++i) {
                if (arm.badL) {
                    L[i] = bad;
                }
                if (arm.badR) {
                    R[i] = bad;
                }
            }

            auto f = bnfMakeBurstField();
            bnfRender(*f, L, R);

            // (1) every output sample finite (bit pattern).
            std::size_t nonFinite = 0;
            for (std::size_t i = 0; i < kBnfTotal; ++i) {
                if (!isFinite(L[i]) || !isFinite(R[i])) {
                    ++nonFinite;
                }
            }
            CHECK(nonFinite == 0);

            // (2) the output mask: a channel whose input sample was non-finite outputs exactly +0.0f
            //     (bits 0; -0.0f fails), the one-channel-bad arms included.
            std::size_t maskMisses = 0;
            std::size_t goodChannelNonZero = 0;
            for (std::size_t i = kBnfBurstStart; i < kBnfBurstEnd; ++i) {
                if (arm.badL && bnfBits(L[i]) != 0u) {
                    ++maskMisses;
                }
                if (arm.badR && bnfBits(R[i]) != 0u) {
                    ++maskMisses;
                }
                if (!arm.badL && bnfBits(L[i]) != 0u) {
                    ++goodChannelNonZero;
                }
                if (!arm.badR && bnfBits(R[i]) != 0u) {
                    ++goodChannelNonZero;
                }
            }
            CHECK(maskMisses == 0);

            // (3) state finite within one block: the block after the burst is finite on both channels,
            //     and the stage's observers are finite at the end of the render.
            bool blockAfterFinite = true;
            for (std::size_t i = kBnfBurstEnd; i < kBnfBurstEnd + kBnfHostBlock; ++i) {
                blockAfterFinite = blockAfterFinite && isFinite(L[i]) && isFinite(R[i]);
            }
            CHECK(blockAfterFinite);
            CHECK(isFinite(f->currentSideGain()));
            CHECK(isFinite(f->modeFade()));
            CHECK(isFinite(f->smoothedCrossoverHz()));
            CHECK(isFinite(f->stageCrossoverHz()));

            // (4) from 100 ms after the burst's end, within -60 dB of the reference over the next 0.5 s.
            const double dbL = bnfDiffDb(L, refL, kBnfRecoveryStart, kBnfRecoveryLen);
            const double dbR = bnfDiffDb(R, refR, kBnfRecoveryStart, kBnfRecoveryLen);
            // Printed beside: the error over the first block after the burst (not gated).
            const double dbFirstL = bnfDiffDb(L, refL, kBnfBurstEnd, kBnfHostBlock);
            const double dbFirstR = bnfDiffDb(R, refR, kBnfBurstEnd, kBnfHostBlock);
            std::printf("SC-014 burst arm=%s value=%s nonfinite_out=%zu mask_misses=%zu good_channel_nonzero=%zu "
                        "first_block_err_dB L=%.1f R=%.1f recovery_err_dB L=%.1f R=%.1f (bound %.0f)\n",
                        arm.name, kBnfBadNames[p], nonFinite, maskMisses, goodChannelNonZero, dbFirstL, dbFirstR,
                        dbL, dbR, kBnfRecoveryBoundDb);
            CHECK(dbL <= kBnfRecoveryBoundDb);
            CHECK(dbR <= kBnfRecoveryBoundDb);
        }
    }
}

// ==============================================================================
// SC-014 / FR-072: a non-finite control keeps the previous value
// ==============================================================================

namespace {

/// Every observable per interval, recorded for the bit-equality comparison.
struct BnfRecord {
    std::vector<float> l;
    std::vector<float> r;
    std::vector<float> motion;       // motionValue() after each advanceControl
    std::vector<float> cycleHz;      // cycleFrequencyHz() after each advanceControl
    std::vector<float> pan;          // computePanVector(f0 55 Hz, 64 partials) after each advanceControl
};

constexpr std::size_t kBnfCtlTotal = 24000;        // 0.5 s
constexpr std::size_t kBnfCtlBadInterval = 300;    // the bad calls land before this interval's advanceControl
constexpr float kBnfCtlTempo = 100.0f;

BnfControls bnfCtlBase() {
    BnfControls c{};
    c.mode = BnfMode::Spread;
    c.spread = 0.7f;
    c.gravity = 0.3f;
    c.crossoverHz = 150.0f;
    c.curve = 0.4f;
    c.motionDepth = 0.6f;
    c.motionRate = 0.5f;
    c.motionSource = BnfSource::Cycle;
    c.cycleSync = true;   // makes the tempo reach the render (cycleFrequencyHz, the law through m)
    c.cycleNote = NoteValue::Quarter;
    c.cycleModifier = NoteModifier::None;
    return c;
}

/// Renders kBnfCtlTotal samples of the noise fixture; badCalls(f) runs once, before the
/// kBnfCtlBadInterval-th advanceControl.
template <class BadCalls>
BnfRecord bnfCtlRender(const BadCalls& badCalls) {
    auto f = std::make_unique<BassStereoField>();
    f->setControls(bnfCtlBase());
    f->setTempoBpm(kBnfCtlTempo);
    f->prepare(kBnfFs, kBnfHostBlock);

    BnfRecord rec;
    bnfNoise(rec.l, rec.r, kBnfCtlTotal);
    BassStereoField::VoiceState vs{};
    std::array<float, Krate::DSP::kMaxPartials> vec{};
    std::size_t interval = 0;
    for (std::size_t pos = 0; pos < kBnfCtlTotal; pos += BassStereoField::kControlInterval) {
        if (interval == kBnfCtlBadInterval) {
            badCalls(*f);
        }
        f->advanceControl();
        rec.motion.push_back(f->motionValue());
        rec.cycleHz.push_back(f->cycleFrequencyHz());
        f->computePanVector(vs, 55.0f, vec, BassStereoField::kDefaultPartials);
        rec.pan.insert(rec.pan.end(), vec.begin(), vec.end());
        const std::size_t len = std::min(BassStereoField::kControlInterval, kBnfCtlTotal - pos);
        f->processBlock(rec.l.data() + pos, rec.r.data() + pos, len);
        ++interval;
    }
    return rec;
}

bool bnfVecBitEqual(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}

}  // namespace

TEST_CASE("BassStereoField_NonFiniteControls", "[systems][profundum][stereo-field][nonfinite][sc-014][fr-072]") {
    const BnfRecord ref = bnfCtlRender([](BassStereoField&) {});

    // Non-vacuity: the render sounds, MOTION moves, and the synced Cycle rate depends on the tempo.
    REQUIRE(bnfRms(ref.l.data(), ref.l.size()) > 0.0);
    REQUIRE(std::any_of(ref.motion.begin(), ref.motion.end(), [](float m) { return m != 0.0f; }));
    REQUIRE(std::any_of(ref.pan.begin(), ref.pan.end(), [](float v) { return v != 0.0f; }));
    {
        auto probe = std::make_unique<BassStereoField>();
        probe->setControls(bnfCtlBase());
        probe->setTempoBpm(kBnfCtlTempo);
        probe->prepare(kBnfFs, kBnfHostBlock);
        probe->advanceControl();
        const float hzAt100 = probe->cycleFrequencyHz();
        probe->setTempoBpm(160.0f);
        probe->advanceControl();
        REQUIRE(bnfBits(probe->cycleFrequencyHz()) != bnfBits(hzAt100));
    }

    // The float Controls fields (FR-002) plus the tempo setter (FR-036).
    enum class Target : std::uint8_t { Spread, Gravity, Crossover, Curve, MotionDepth, MotionRate, Tempo };
    constexpr std::array<Target, 7> kTargets{Target::Spread,      Target::Gravity,    Target::Crossover, Target::Curve,
                                             Target::MotionDepth, Target::MotionRate, Target::Tempo};
    constexpr std::array<const char*, 7> kTargetNames{"spread",      "gravity",    "crossoverHz", "curve",
                                                      "motionDepth", "motionRate", "setTempoBpm"};

    for (std::size_t t = 0; t < kTargets.size(); ++t) {
        for (std::size_t p = 0; p < kBnfBadBits.size(); ++p) {
            INFO("target " << kTargetNames[t] << ", value " << kBnfBadNames[p]);
            const float bad = bnfFromBits(kBnfBadBits[p]);
            REQUIRE_FALSE(isFinite(bad));

            bool controlsKept = false;
            bool cycleHzKept = false;
            const BnfRecord got = bnfCtlRender([&](BassStereoField& f) {
                const BnfControls before = f.controls();
                const float hzBefore = f.cycleFrequencyHz();
                if (kTargets[t] == Target::Tempo) {
                    f.setTempoBpm(bad);
                } else {
                    BnfControls c = before;
                    switch (kTargets[t]) {
                        case Target::Spread: c.spread = bad; break;
                        case Target::Gravity: c.gravity = bad; break;
                        case Target::Crossover: c.crossoverHz = bad; break;
                        case Target::Curve: c.curve = bad; break;
                        case Target::MotionDepth: c.motionDepth = bad; break;
                        case Target::MotionRate: c.motionRate = bad; break;
                        case Target::Tempo: break;
                    }
                    f.setControls(c);
                }
                controlsKept = bnfControlsBitEqual(f.controls(), before);
                cycleHzKept = bnfBits(f.cycleFrequencyHz()) == bnfBits(hzBefore);
            });

            CHECK(controlsKept);   // controls() bit-equal right after the bad call
            CHECK(cycleHzKept);
            // cycleFrequencyHz() unchanged once the bad call has been latched (every interval bit-equal).
            CHECK(bnfVecBitEqual(got.cycleHz, ref.cycleHz));
            // The following render is bit-equal to the one that never saw the bad call.
            const bool lEq = bnfVecBitEqual(got.l, ref.l);
            const bool rEq = bnfVecBitEqual(got.r, ref.r);
            const bool motionEq = bnfVecBitEqual(got.motion, ref.motion);
            const bool panEq = bnfVecBitEqual(got.pan, ref.pan);
            std::printf("SC-014 control target=%s value=%s controls_kept=%d cycle_hz_kept=%d L_eq=%d R_eq=%d "
                        "motion_eq=%d pan_eq=%d\n",
                        kTargetNames[t], kBnfBadNames[p], controlsKept ? 1 : 0, cycleHzKept ? 1 : 0, lEq ? 1 : 0,
                        rEq ? 1 : 0, motionEq ? 1 : 0, panEq ? 1 : 0);
            CHECK(lEq);
            CHECK(rEq);
            CHECK(motionEq);
            CHECK(panEq);
        }
    }
}
