// ==============================================================================
// Profundum Phase 14 (specs/profundum-phase14-morph-modulation) - profundum_engine_morphmod_test.cpp
// ==============================================================================
// ProfundumEngine morph-modulation cases. Placeholder written by T002; later tasks fill it
// (all [systems][profundum][morphmod][voice-engine]):
//   T007: ProfundumEngine_MorphModComposition        (SC-005)
//         ProfundumEngine_MorphModTransparentAtZero  (SC-006)
//   T008: ProfundumEngine_MorphModVelocity           (SC-004)
//         ProfundumEngine_MorphModNoAlloc            (SC-001 (b))
//         ProfundumEngine_MorphModRetrigger          (SC-010)
//         ProfundumEngine_MorphModLifecycle          (SC-012 (b)(c)(c'), SC-003 (d), SC-013 (e)(h))
//         ProfundumEngine_MorphModAnchorMono         (SC-019 engine)
//   T009: ProfundumEngine_MorphModClickFree_<k>of8   (SC-011, k = 1..8)
//
// Every render goes through the Phase 9 driver (profundum_voice_engine_test_helpers.h) with the Phase 14 script
// fields / events (plan 4.4). The NaN-injection arms of SC-005 (e) live in profundum_morph_mod_nonfinite_test.cpp
// (dsp_ieee_tests, dsp/CLAUDE.md); the transparent arms are compared bitwise only within this binary (same code, same
// build).
// ==============================================================================

#include <krate/dsp/core/note_value.h>
#include <krate/dsp/primitives/adsr_envelope.h>
#include <krate/dsp/primitives/envelope_utils.h>
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/processors/mono_handler.h>
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/profundum_engine.h>
#include <krate/dsp/systems/profundum_engine_observer.h>
#include <krate/dsp/systems/profundum_morph_modulator.h>

#include "bass_morph_state_helpers.h"
#include "profundum_audibility_readers.h"
#include "profundum_voice_engine_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <enable_ftz_daz.h>

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <utility>
#include <vector>

using namespace Krate::DSP;

namespace {

namespace VE = Krate::DSP::ProfundumTest;
namespace auRd = Krate::DSP::ProfundumAudibility;

using MmConfig = ProfundumMorphModulator::Config;
using MmSource = ProfundumMorphModulator::Source;
using MmKind = VE::VoiceEngineEventKind;

constexpr double kMmFs = 48000.0;
/// 0.4 s: the Square 2 Hz Cycle flips at 0.25 s, the Envelope (A 0.1 / D 300 / S 0) has decayed by 0.3 s.
constexpr std::uint64_t kMmCompositionSamples = 19200;
/// SC-006: 2 s.
constexpr std::uint64_t kMmTransparencySamples = 96000;
constexpr double kMmTargetTol = 1.0e-6;   // SC-005 (a): a recomputation, never a bit-equality claim
constexpr double kMmFlEps = static_cast<double>(std::numeric_limits<float>::epsilon());

constexpr std::array<MmSource, 4> kMmSources{MmSource::Envelope, MmSource::Cycle, MmSource::Velocity,
                                             MmSource::ModWheel};
constexpr std::array<float, 4> kMmAmounts{-1.0f, -0.5f, 0.5f, 1.0f};
constexpr std::array<float, 3> kMmBases{0.0f, 0.5f, 1.0f};
constexpr std::array<float, 3> kMmRampsMs{5.0f, 40.0f, 2000.0f};

[[nodiscard]] bool mmSame(float a, float b) noexcept {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

[[nodiscard]] const char* mmSourceName(MmSource s) noexcept {
    switch (s) {
        case MmSource::Off: return "Off";
        case MmSource::Envelope: return "Envelope";
        case MmSource::Cycle: return "Cycle";
        case MmSource::Velocity: return "Velocity";
        case MmSource::ModWheel: return "ModWheel";
    }
    return "?";
}

/// SC-005 (d): Cycle Square 2 Hz, Envelope A 0.1 / D 300 / S 0, so the target steps.
[[nodiscard]] MmConfig mmStepConfig(MmSource src, float amount) noexcept {
    MmConfig c{};
    c.source = src;
    c.amount = amount;
    c.envAttackMs = 0.1f;
    c.envDecayMs = 300.0f;
    c.envSustain = 0.0f;
    c.cycleShape = Waveform::Square;
    c.cycleRateHz = 2.0f;
    return c;
}

/// The showcase pair A_s / B (MONO, E1, 48 kHz, host block 64, one held note from sample 0) at position b, the ramp
/// set at sample 0 (before the first grid point), the modulation handed through the Phase 14 script fields.
[[nodiscard]] VE::VoiceEngineScript mmCompositionScript(float b, float rampMs, const MmConfig& c, float wheel,
                                                        bool bypass, std::uint64_t total = kMmCompositionSamples) {
    VE::VoiceEngineScript s = auRd::auScript(
        auRd::AuSetting{.a = auRd::auStateAs(), .b = auRd::auStateB(), .position = b}, auRd::kAuE1, total);
    REQUIRE(s.fs == kMmFs);
    REQUIRE(s.hostBlock == 64u);
    s.events.push_back(VE::VoiceEngineEvent{.sample = 0, .kind = MmKind::MorphRampMs, .value = rampMs});
    s.morphMod = c;
    s.morphModWheel = wheel;
    s.morphModBypass = bypass;
    return s;
}

/// FR-020: Delta p_max = 32 000 / (rampMs * fs), in double.
[[nodiscard]] double mmDeltaMax(double rampMs, double fs) noexcept {
    return 32000.0 / (rampMs * fs);
}

/// The Phase 8 B3-1 bar (bass_morph_engine_test.cpp:69-72, :207-213): Delta p_max (1 + 4 FLT_EPSILON) + 0.5 ulp of
/// the larger of the two positions (SlewLimiter adds the step in float).
[[nodiscard]] double mmStepBar(double dMax, float before, float after) {
    const float larger = std::fabs(before) > std::fabs(after) ? before : after;
    return dMax * (1.0 + 4.0 * kMmFlEps) + 0.5 * static_cast<double>(BassMorphTest::ulpOf(larger));
}

/// -0.0f from a volatile bit pattern (no literal the fast-math leg could fold to +0).
[[nodiscard]] float mmNegativeZero() noexcept {
    volatile std::uint32_t z = 0x80000000u;
    return std::bit_cast<float>(static_cast<std::uint32_t>(z));
}

[[nodiscard]] bool mmIdentical(const std::vector<float>& x, const std::vector<float>& y) noexcept {
    return x.size() == y.size() && std::memcmp(x.data(), y.data(), x.size() * sizeof(float)) == 0;
}

}  // namespace

// ------------------------------------------------------------------------------
// SC-005: composition (FR-010 - FR-012, FR-006 engine level)
// ------------------------------------------------------------------------------

TEST_CASE("ProfundumEngine_MorphModComposition", "[systems][profundum][morphmod][voice-engine]") {
    enableFTZDAZ();

    // (a) target = clamp(b + A s, 0, 1) at every grid point; (c) p in [0, 1]; (d) |dp| within the B3-1 bar, with the
    // non-vacuity arm: an arm whose target leaves b by more than Delta p_max takes >= 1 step above 0.5 Delta p_max.
    std::array<std::size_t, kMmSources.size()> movingArms{};
    std::size_t arms = 0;
    for (std::size_t si = 0; si < kMmSources.size(); ++si) {
        const MmSource src = kMmSources[si];
        for (const float amount : kMmAmounts) {
            for (const float b : kMmBases) {
                for (const float rampMs : kMmRampsMs) {
                    const float wheel = src == MmSource::ModWheel ? 1.0f : 0.0f;   // the wheel moves the target
                    const VE::VoiceEngineTaps t =
                        VE::voiceEngineRender(mmCompositionScript(b, rampMs, mmStepConfig(src, amount), wheel, false));
                    ++arms;
                    INFO("source " << mmSourceName(src) << ", A " << amount << ", b " << b << ", ramp " << rampMs
                                   << " ms");
                    REQUIRE(!t.grid.empty());
                    const double dMax = mmDeltaMax(static_cast<double>(rampMs), kMmFs);
                    std::size_t targetMisses = 0;
                    std::size_t outOfRange = 0;
                    std::size_t stepViolations = 0;
                    std::size_t bigSteps = 0;
                    double worstTargetErr = 0.0;
                    double worstStepRatio = 0.0;
                    double maxLeave = 0.0;
                    float prev = b;   // prepare snaps p to the base (P-1)
                    for (const VE::VoiceEngineGridRecord& r : t.grid) {
                        const float expected = std::clamp(b + amount * r.morphModSource, 0.0f, 1.0f);
                        const double err =
                            std::fabs(static_cast<double>(r.morphTarget) - static_cast<double>(expected));
                        worstTargetErr = std::max(worstTargetErr, err);
                        if (err > kMmTargetTol)
                            ++targetMisses;
                        maxLeave = std::max(maxLeave,
                                            std::fabs(static_cast<double>(r.morphTarget) - static_cast<double>(b)));
                        const float p = r.resolved.position;
                        if (p < 0.0f || p > 1.0f)
                            ++outOfRange;
                        const double dp = std::fabs(static_cast<double>(p) - static_cast<double>(prev));
                        worstStepRatio = std::max(worstStepRatio, dp / dMax);
                        if (dp > mmStepBar(dMax, prev, p))
                            ++stepViolations;
                        if (dp > 0.5 * dMax)
                            ++bigSteps;
                        prev = p;
                    }
                    INFO("worst |target - clamp(b + A s)| " << worstTargetErr << ", worst |dp| / dp_max "
                                                            << worstStepRatio << ", max |target - b| " << maxLeave
                                                            << ", steps > 0.5 dp_max " << bigSteps);
                    CHECK(targetMisses == 0u);
                    CHECK(outOfRange == 0u);
                    CHECK(stepViolations == 0u);
                    if (maxLeave > dMax) {
                        CHECK(bigSteps >= 1u);
                        ++movingArms[si];
                    }
                }
            }
        }
    }
    CHECK(arms == kMmSources.size() * kMmAmounts.size() * kMmBases.size() * kMmRampsMs.size());
    // Non-vacuity per source: the clamp holds the target at b only on the arms that push past an edge
    // (b 0 with A s <= 0, b 1 with A s >= 0); every source has arms that move.
    for (std::size_t si = 0; si < kMmSources.size(); ++si) {
        INFO("source " << mmSourceName(kMmSources[si]) << ": moving arms " << movingArms[si]);
        CHECK(movingArms[si] >= 1u);
    }

    // (b) transparent: Source Off, A = +0, A = -0, or bypass on with A = 1 -> the target is b bit for bit.
    {
        const float negZero = mmNegativeZero();
        REQUIRE(std::bit_cast<std::uint32_t>(negZero) == 0x80000000u);
        struct TransparentArm {
            MmSource source;
            float amount;
            bool bypass;
            const char* what;
        };
        std::vector<TransparentArm> transparentArms{{.source=MmSource::Off, .amount=1.0f, .bypass=false, .what="Source Off, A 1"}};
        for (const MmSource src : kMmSources) {
            transparentArms.push_back({.source=src, .amount=0.0f, .bypass=false, .what="A +0"});
            transparentArms.push_back({.source=src, .amount=negZero, .bypass=false, .what="A -0"});
            transparentArms.push_back({.source=src, .amount=1.0f, .bypass=true, .what="bypass, A 1"});
        }
        for (const TransparentArm& arm : transparentArms) {
            for (const float b : kMmBases) {
                const VE::VoiceEngineTaps t = VE::voiceEngineRender(
                    mmCompositionScript(b, 40.0f, mmStepConfig(arm.source, arm.amount), 1.0f, arm.bypass));
                INFO("transparent arm: " << arm.what << ", source " << mmSourceName(arm.source) << ", b " << b);
                REQUIRE(!t.grid.empty());
                std::size_t notBase = 0;
                for (const VE::VoiceEngineGridRecord& r : t.grid) {
                    if (!mmSame(r.morphTarget, b))
                        ++notBase;
                }
                CHECK(notBase == 0u);
            }
        }
    }

    // (e) FR-006 at the engine level: Mod Wheel source, A 1, b 0; the wheel events land on grid points. The NaN
    // event and the NaN read-back: ProfundumEngine_MorphModNonFinite (dsp_ieee_tests).
    {
        constexpr std::uint64_t kAt1 = 3200;
        constexpr std::uint64_t kAt2 = 6400;
        constexpr std::uint64_t kAt3 = 9600;
        constexpr std::uint64_t kAt4 = 12800;
        VE::VoiceEngineScript s = mmCompositionScript(0.0f, 40.0f, mmStepConfig(MmSource::ModWheel, 1.0f), 0.0f,
                                                      false, 16000);
        s.events.push_back(VE::VoiceEngineEvent{.sample = kAt1, .kind = MmKind::MorphModWheel, .value = 1.5f});
        s.events.push_back(VE::VoiceEngineEvent{.sample = kAt2, .kind = MmKind::MorphModWheel, .value = -0.2f});
        s.events.push_back(VE::VoiceEngineEvent{.sample = kAt3, .kind = MmKind::MorphModWheel, .value = 0.4f});
        const VE::VoiceEngineTaps t = VE::voiceEngineRender(s);
        const auto sourceAt = [&t](std::uint64_t sample) {
            for (const VE::VoiceEngineGridRecord& r : t.grid) {
                if (r.sample == sample)
                    return r.morphModSource;
            }
            FAIL("no grid record at sample " << sample);
            return -1.0f;
        };
        CHECK(sourceAt(kAt1) == 1.0f);
        CHECK(sourceAt(kAt2) == 0.0f);
        CHECK(sourceAt(kAt3) == 0.4f);
        CHECK(sourceAt(kAt4) == 0.4f);
        CHECK(sourceAt(kAt4 + 32) == 0.4f);   // the wheel stays latched with no further event
        // Before the first event the wheel is the script's 0.
        CHECK(sourceAt(0) == 0.0f);

        // The read-back of the pending wheel (FR-066 pattern).
        auto e = std::make_unique<ProfundumEngine>();
        e->setMorphModWheel(1.5f);
        CHECK(e->morphModWheel() == 1.0f);
        e->setMorphModWheel(-0.2f);
        CHECK(e->morphModWheel() == 0.0f);
        e->setMorphModWheel(0.4f);
        CHECK(e->morphModWheel() == 0.4f);
    }
}

// ------------------------------------------------------------------------------
// SC-006: transparent at zero (FR-012, FR-060)
// ------------------------------------------------------------------------------

TEST_CASE("ProfundumEngine_MorphModTransparentAtZero", "[systems][profundum][morphmod][voice-engine]") {
    enableFTZDAZ();

    struct MmPair {
        MorphState a{};
        MorphState b{};
        const char* name = "";
        bool distinct = false;   // endpoints differ, so a moving target changes the sound (non-vacuity arm)
    };
    MorphState foldA = auRd::auStateAs();
    foldA.foldAmount = 60.0f;
    MorphState foldB = foldA;
    foldB.foldAmount = 100.0f;
    foldB.field.mode = BassStereoField::ImageMode::Wide;
    const std::array<MmPair, 3> pairs{
        MmPair{.a = MorphState{}, .b = MorphState{}, .name = "default MorphState{} pair", .distinct = false},
        MmPair{.a = auRd::auStateAs(), .b = auRd::auStateB(), .name = "showcase A_s / B", .distinct = true},
        MmPair{.a = foldA, .b = foldB, .name = "Fold 60 / Fold 100 + Wide", .distinct = true},
    };
    struct MmMode {
        ProfundumEngine::VoiceMode mode;
        std::size_t polyphony;
        const char* name;
    };
    const std::array<MmMode, 2> modes{MmMode{.mode=ProfundumEngine::VoiceMode::Mono, .polyphony=1, .name="MONO"},
                                      MmMode{.mode=ProfundumEngine::VoiceMode::Poly, .polyphony=4, .name="POLY4"}};
    const std::array<std::uint8_t, 2> notes{auRd::kAuE1, auRd::kAuC3};

    const auto render = [](const MmPair& p, const MmMode& m, std::uint8_t note, const MmConfig& c, bool bypass) {
        VE::VoiceEngineScript s = auRd::auScript(auRd::AuSetting{.a = p.a, .b = p.b, .position = 0.5f}, note,
                                                 kMmTransparencySamples);
        s.mode = m.mode;
        s.polyphony = m.polyphony;
        s.attachObserver = false;   // the output only
        s.morphMod = c;
        s.morphModWheel = 0.0f;
        s.morphModBypass = bypass;
        return VE::voiceEngineRender(s);
    };
    const auto config = [](MmSource src, float amount) {
        MmConfig c{};
        c.source = src;
        c.amount = amount;
        return c;
    };

    std::size_t nonVacuityArms = 0;
    for (const MmMode& m : modes) {
        for (const MmPair& p : pairs) {
            REQUIRE(isValidMorphState(p.a));
            REQUIRE(isValidMorphState(p.b));
            for (const std::uint8_t note : notes) {
                INFO(m.name << ", " << p.name << ", note " << static_cast<int>(note));
                const VE::VoiceEngineTaps off = render(p, m, note, config(MmSource::Off, 0.0f), false);
                for (const MmSource src : kMmSources) {
                    INFO("Amount 0 arm: " << mmSourceName(src));
                    const VE::VoiceEngineTaps t = render(p, m, note, config(src, 0.0f), false);
                    CHECK(mmIdentical(t.outL, off.outL));
                    CHECK(mmIdentical(t.outR, off.outR));
                }
                {
                    INFO("bypass arm: Envelope, Amount 1");
                    const VE::VoiceEngineTaps t = render(p, m, note, config(MmSource::Envelope, 1.0f), true);
                    CHECK(mmIdentical(t.outL, off.outL));
                    CHECK(mmIdentical(t.outR, off.outR));
                }
                if (p.distinct) {
                    // Non-vacuity: Envelope at Amount 1 without bypass moves the target off 0.5 and changes the sound.
                    INFO("non-vacuity arm: Envelope, Amount 1, no bypass");
                    const VE::VoiceEngineTaps t = render(p, m, note, config(MmSource::Envelope, 1.0f), false);
                    CHECK_FALSE((mmIdentical(t.outL, off.outL) && mmIdentical(t.outR, off.outR)));
                    ++nonVacuityArms;
                }
            }
        }
    }
    CHECK(nonVacuityArms == modes.size() * 2u * notes.size());
}

// ==============================================================================
// T008: engine behaviour (SC-001 (b), SC-003 (d) engine half, SC-004, SC-010 (a)-(e)(g)(h), SC-012 (b)(c)(c'),
// SC-013 (e) engine half, (h), SC-019 engine). No production code: a red here is a T007 defect.
// ==============================================================================

namespace {

using MmVoiceMode = ProfundumEngine::VoiceMode;

constexpr double kMmTraceTol = 1.0e-6;   // the 1e-6 trace bound (SC-004, SC-010, SC-013): a recomputation, never bits
constexpr std::uint64_t kMmGrid = VE::kVoiceEngineGrid;
constexpr std::uint8_t kMmNoteE1 = VE::kVoiceEngineNoteE1;
constexpr std::uint8_t kMmNoteA1 = VE::kVoiceEngineNoteA1;
constexpr std::uint8_t kMmNoteE2 = VE::kVoiceEngineNoteE2;
constexpr std::uint8_t kMmNoteC3 = VE::kVoiceEngineNoteC3;
/// SC-010 note times: 0.1 s apart, so an Attack of 20 ms (30 intervals) has peaked and s > 0.5 before each next note.
constexpr std::uint64_t kMmRtN1 = 0;
constexpr std::uint64_t kMmRtN2 = 4800;
constexpr std::uint64_t kMmRtN3 = 9600;

struct MmModeArm {
    MmVoiceMode mode;
    std::size_t polyphony;
    const char* name;
};
constexpr std::array<MmModeArm, 2> kMmModeArms{MmModeArm{.mode=MmVoiceMode::Mono, .polyphony=1, .name="MONO"},
                                               MmModeArm{.mode=MmVoiceMode::Poly, .polyphony=4, .name="POLY4"}};

struct MmChordNote {
    std::uint8_t note;
    std::uint8_t velocity;
};

/// A script without auScript's note at sample 0: the showcase pair A_s / B at position b, 48 kHz, host block 64,
/// the observer attached, per-sample voice taps for [0, tapped), the modulation handed through the script fields.
[[nodiscard]] VE::VoiceEngineScript mmBareScript(MmVoiceMode mode, std::size_t polyphony, std::uint64_t total,
                                                 const MmConfig& c, float b = 0.5f, std::size_t tapped = 0) {
    VE::VoiceEngineScript s;
    s.fs = kMmFs;
    s.hostBlock = 64;
    s.totalSamples = total;
    s.a = auRd::auStateAs();
    s.b = auRd::auStateB();
    s.position = b;
    s.mode = mode;
    s.polyphony = polyphony;
    s.attachObserver = true;
    s.tappedVoices = tapped;
    s.morphMod = c;
    return s;
}

[[nodiscard]] VE::VoiceEngineEvent mmOn(std::uint64_t at, std::uint8_t note, std::uint8_t velocity = 100) {
    return VE::VoiceEngineEvent{.sample = at, .kind = MmKind::NoteOn, .note = note, .velocity = velocity};
}

[[nodiscard]] VE::VoiceEngineEvent mmOff(std::uint64_t at, std::uint8_t note) {
    return VE::VoiceEngineEvent{.sample = at, .kind = MmKind::NoteOff, .note = note};
}

[[nodiscard]] VE::VoiceEngineEvent mmSetIndex(std::uint64_t at, MmKind kind, int index) {
    return VE::VoiceEngineEvent{.sample = at, .kind = kind, .index = index};
}

[[nodiscard]] VE::VoiceEngineEvent mmSetValue(std::uint64_t at, MmKind kind, float value) {
    return VE::VoiceEngineEvent{.sample = at, .kind = kind, .value = value};
}

[[nodiscard]] MmConfig mmSourceConfig(MmSource src, float amount = 1.0f) noexcept {
    MmConfig c{};
    c.source = src;
    c.amount = amount;
    return c;
}

[[nodiscard]] MmConfig mmEnvConfig(float attackMs, float decayMs, float sustain) noexcept {
    MmConfig c = mmSourceConfig(MmSource::Envelope);
    c.envAttackMs = attackMs;
    c.envDecayMs = decayMs;
    c.envSustain = sustain;
    return c;
}

/// The C-4 restart observable: the first process() of a fresh reference ADSREnvelope at fs / 32 (prepared, Hard
/// retrigger, the same A / D / S, reset(); gate(true)), as plan 4.1 sets up the modulator's envelope.
[[nodiscard]] float mmRestartObservable(double fs, const MmConfig& c) noexcept {
    ADSREnvelope ref;
    ref.prepare(static_cast<float>(fs / static_cast<double>(ProfundumMorphModulator::kControlInterval)));
    ref.setRetriggerMode(RetriggerMode::Hard);
    ref.setAttack(c.envAttackMs);
    ref.setDecay(c.envDecayMs);
    ref.setSustain(c.envSustain);
    ref.reset();
    ref.gate(true);
    return ref.process();
}

/// The index of the grid record at `sample` (records start at sample 0, one per interval).
[[nodiscard]] std::size_t mmGridIndex(const VE::VoiceEngineTaps& t, std::uint64_t sample) {
    REQUIRE(sample % kMmGrid == 0u);
    const auto k = static_cast<std::size_t>(sample / kMmGrid);
    REQUIRE(k < t.grid.size());
    REQUIRE(t.grid[k].sample == sample);
    return k;
}

[[nodiscard]] std::vector<float> mmSourceTrace(const VE::VoiceEngineTaps& t) {
    std::vector<float> v;
    v.reserve(t.grid.size());
    for (const VE::VoiceEngineGridRecord& r : t.grid)
        v.push_back(r.morphModSource);
    return v;
}

[[nodiscard]] double mmMaxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    REQUIRE(a.size() == b.size());
    double worst = 0.0;
    for (std::size_t k = 0; k < a.size(); ++k)
        worst = std::max(worst, std::fabs(static_cast<double>(a[k]) - static_cast<double>(b[k])));
    return worst;
}

/// SC-010: the note-on's grid point reads the restart observable within 1e-6; with `heldBefore`, the interval before
/// reads s > 0.5 (the non-vacuity arm: a restart is visible as a drop).
void mmCheckRestart(const VE::VoiceEngineTaps& t, std::uint64_t sample, float restart, bool heldBefore) {
    const std::size_t k = mmGridIndex(t, sample);
    const float s = t.grid[k].morphModSource;
    INFO("note-on grid point " << sample << ": s " << s << ", restart observable " << restart);
    CHECK(std::fabs(static_cast<double>(s) - static_cast<double>(restart)) <= kMmTraceTol);
    if (heldBefore) {
        REQUIRE(k > 0u);
        const float prev = t.grid[k - 1].morphModSource;
        INFO("s the interval before " << prev);
        CHECK(prev > 0.5f);
        CHECK(s < prev);
    }
}

/// Mean period between upward zero crossings of a grid trace, linear interpolation between grid points (plan 5.3).
struct MmPeriod {
    double seconds = 0.0;
    std::size_t cycles = 0;
};

[[nodiscard]] MmPeriod mmUpwardCrossingPeriod(const std::vector<float>& trace, double fc) noexcept {
    double first = 0.0;
    double last = 0.0;
    std::size_t crossings = 0;
    for (std::size_t k = 1; k < trace.size(); ++k) {
        const double a = static_cast<double>(trace[k - 1]);
        const double b = static_cast<double>(trace[k]);
        if (a < 0.0 && b >= 0.0) {
            const double x = static_cast<double>(k - 1) + (-a) / (b - a);
            if (crossings == 0)
                first = x;
            last = x;
            ++crossings;
        }
    }
    MmPeriod p;
    if (crossings >= 2) {
        p.cycles = crossings - 1;
        p.seconds = (last - first) / static_cast<double>(p.cycles) / fc;
    }
    return p;
}

[[nodiscard]] bool mmConfigFieldEqual(const MmConfig& x, const MmConfig& y) noexcept {
    return x.source == y.source && mmSame(x.amount, y.amount) && mmSame(x.envAttackMs, y.envAttackMs)
           && mmSame(x.envDecayMs, y.envDecayMs) && mmSame(x.envSustain, y.envSustain)
           && x.cycleShape == y.cycleShape && mmSame(x.cycleRateHz, y.cycleRateHz) && x.cycleSync == y.cycleSync
           && x.cycleNote == y.cycleNote && x.cycleModifier == y.cycleModifier;
}

/// A grid-only observer for the cases that drive the engine's lifecycle by hand (prepare / reset / restore between
/// blocks), which the Phase 9 driver does not expose. Reserve the vector before rendering.
struct MmGridPoint {
    std::uint64_t sample = 0;
    float source = 0.0f;
};

class MmGridObserver final : public ProfundumEngineObserver {
public:
    explicit MmGridObserver(std::vector<MmGridPoint>& out) noexcept : out_(&out) {}
    void onGrid(const ProfundumGridRecord& g) noexcept override {
        out_->push_back(MmGridPoint{.sample = g.sample, .source = g.morphModSource});
    }

private:
    std::vector<MmGridPoint>* out_;
};

/// A heap engine with the showcase pair A_s / B, base position b and the modulation config (not yet prepared).
[[nodiscard]] std::unique_ptr<ProfundumEngine> mmMakeEngine(const MmConfig& c, float b) {
    auto e = std::make_unique<ProfundumEngine>();
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::A, auRd::auStateAs()));
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::B, auRd::auStateB()));
    e->setMorphPosition(b);
    e->setMorphModulation(c);
    return e;
}

/// `blocks` host blocks of 64 samples (two grid points each), output discarded.
void mmRenderBlocks(ProfundumEngine& e, std::size_t blocks) noexcept {
    std::array<float, 64> l{};
    std::array<float, 64> r{};
    for (std::size_t i = 0; i < blocks; ++i)
        e.processBlock(l.data(), r.data(), l.size());
}

}  // namespace

// ------------------------------------------------------------------------------
// SC-004: the Velocity latch (FR-013, C-6), and a dropped note-on changes nothing
// ------------------------------------------------------------------------------

TEST_CASE("ProfundumEngine_MorphModVelocity", "[systems][profundum][morphmod][voice-engine]") {
    enableFTZDAZ();

    constexpr std::uint64_t kFirst = 4800;                  // v 1
    constexpr std::uint64_t kFirstOff = 9600;
    constexpr std::uint64_t kSecond = kFirstOff + 96000;    // 2 s of silence after the note-off: v 64
    constexpr std::uint64_t kSecondOff = kSecond + 4800;
    constexpr std::uint64_t kThird = kSecondOff + 4800;     // v 127
    constexpr std::uint64_t kThirdOff = kThird + 4800;
    constexpr std::uint64_t kBurstGrid = kThirdOff + 4800;  // the queue is empty at this interval start
    constexpr std::uint64_t kBurst = kBurstGrid + 1;        // one sample inside the interval
    constexpr std::uint64_t kBurstApplied = kBurstGrid + kMmGrid;
    constexpr std::uint64_t kTotal = kBurstApplied + 40 * kMmGrid;
    static_assert(kFirst % kMmGrid == 0 && kSecond % kMmGrid == 0 && kThird % kMmGrid == 0
                  && kBurstGrid % kMmGrid == 0);

    const MmConfig c = mmSourceConfig(MmSource::Velocity);
    for (const MmModeArm& arm : kMmModeArms) {
        INFO(arm.name);
        VE::VoiceEngineScript s = mmBareScript(arm.mode, arm.polyphony, kTotal, c);
        s.events.push_back(mmOn(kFirst, kMmNoteE1, 1));
        s.events.push_back(mmOff(kFirstOff, kMmNoteE1));
        s.events.push_back(mmOn(kSecond, kMmNoteA1, 64));
        s.events.push_back(mmOff(kSecondOff, kMmNoteA1));
        s.events.push_back(mmOn(kThird, kMmNoteC3, 127));
        s.events.push_back(mmOff(kThirdOff, kMmNoteC3));
        // 257 note-ons at one sample: 256 fill the queue (kMaxQueuedEvents) at velocity 64, the 257th (velocity 120)
        // is dropped.
        for (std::size_t i = 0; i < ProfundumEngine::kMaxQueuedEvents; ++i)
            s.events.push_back(mmOn(kBurst, static_cast<std::uint8_t>(36 + (i % 48)), 64));
        s.events.push_back(
            mmOn(kBurst, static_cast<std::uint8_t>(36 + (ProfundumEngine::kMaxQueuedEvents % 48)), 120));
        const VE::VoiceEngineTaps t = VE::voiceEngineRender(s);

        // 0.0f before the first note.
        const std::size_t kF = mmGridIndex(t, kFirst);
        std::size_t nonZeroBefore = 0;
        for (std::size_t k = 0; k < kF; ++k) {
            if (t.grid[k].morphModSource != 0.0f)
                ++nonZeroBefore;
        }
        CHECK(nonZeroBefore == 0u);

        // v 1 at its grid point; unchanged through the note-off and 2 s of silence.
        const float v1 = t.grid[kF].morphModSource;
        INFO("v 1 reads " << v1);
        CHECK(std::fabs(static_cast<double>(v1) - 1.0 / 127.0) <= kMmTraceTol);
        const std::size_t kS = mmGridIndex(t, kSecond);
        std::size_t moved = 0;
        for (std::size_t k = kF; k < kS; ++k) {
            if (!mmSame(t.grid[k].morphModSource, v1))
                ++moved;
        }
        CHECK(moved == 0u);

        // Changes at the next note-on's grid point: v 64, then v 127 exactly 1 (by branch).
        CHECK(std::fabs(static_cast<double>(t.grid[kS].morphModSource) - 64.0 / 127.0) <= kMmTraceTol);
        CHECK(t.grid[mmGridIndex(t, kThird)].morphModSource == 1.0f);
        CHECK(t.grid[mmGridIndex(t, kBurstGrid)].morphModSource == 1.0f);   // the v 127 latch until the burst

        // The dropped note-on: the engine counts drops from its single prepare, so "rose by exactly 1" is == 1; the
        // last accepted velocity (64) is the latch, not the dropped 120.
        REQUIRE(t.droppedEvents == 1u);
        const float burst = t.grid[mmGridIndex(t, kBurstApplied)].morphModSource;
        INFO("burst grid point reads " << burst);
        CHECK(std::fabs(static_cast<double>(burst) - 64.0 / 127.0) <= kMmTraceTol);
    }
}

// ------------------------------------------------------------------------------
// SC-001 (b): no allocation on the audio path with the modulation moving in every host block
// ------------------------------------------------------------------------------

TEST_CASE("ProfundumEngine_MorphModNoAlloc", "[systems][profundum][morphmod][voice-engine]") {
    enableFTZDAZ();

    constexpr std::uint64_t kTotal = 12032;     // 188 host blocks of 64
    constexpr std::uint64_t kNoteEvery = 2400;  // note-ons (with offs) so the restart and latch paths run
    std::size_t arms = 0;
    for (const MmModeArm& arm : kMmModeArms) {
        for (const MmSource src : kMmSources) {
            for (const float amount : {-1.0f, 1.0f}) {
                INFO(arm.name << ", source " << mmSourceName(src) << ", A " << amount);
                const MmConfig c0 = mmStepConfig(src, amount);
                MmConfig c1 = c0;
                c1.envAttackMs = 30.0f;
                c1.envDecayMs = 120.0f;
                c1.envSustain = 0.6f;
                c1.cycleShape = Waveform::SmoothRandom;
                c1.cycleRateHz = 7.0f;
                c1.cycleSync = true;
                c1.cycleNote = NoteValue::Sixteenth;
                VE::VoiceEngineScript s = mmBareScript(arm.mode, arm.polyphony, kTotal, c0);
                s.attachObserver = false;     // the output only; the scope counts the render loop
                s.countAllocations = true;
                s.morphModStates = {c0, c1};
                const std::uint64_t blocks = (kTotal + s.hostBlock - 1) / s.hostBlock;
                for (std::uint64_t k = 0; k < blocks; ++k) {
                    const std::uint64_t at = k * s.hostBlock + 5;
                    s.events.push_back(mmSetIndex(at, MmKind::MorphModulation, static_cast<int>(k % 2)));
                    s.events.push_back(mmSetValue(at, MmKind::MorphModWheel, 0.25f * static_cast<float>(k % 5)));
                    s.events.push_back(mmSetIndex(at, MmKind::MorphModBypass, static_cast<int>((k / 3) % 2)));
                }
                std::uint64_t i = 0;
                for (std::uint64_t at = 0; at < kTotal; at += kNoteEvery, ++i) {
                    const std::uint8_t note = (i % 2 == 0) ? kMmNoteE1 : kMmNoteA1;
                    s.events.push_back(mmOn(at, note, static_cast<std::uint8_t>(40 + 20 * (i % 4))));
                    s.events.push_back(mmOff(at + kNoteEvery / 2, note));
                }
                const VE::VoiceEngineTaps t = VE::voiceEngineRender(s);
                CHECK(t.allocations == 0u);
                ++arms;
            }
        }
    }
    CHECK(arms == kMmModeArms.size() * kMmSources.size() * 2u);
}

// ------------------------------------------------------------------------------
// SC-010: the Envelope restarts on every accepted note-on; nothing else touches the sources (C-4, C-5, FR-013)
// ------------------------------------------------------------------------------

TEST_CASE("ProfundumEngine_MorphModRetrigger", "[systems][profundum][morphmod][voice-engine]") {
    enableFTZDAZ();

    // A >= 10 ms, Sustain 1: s > 0.5 the interval before every later note (the restart non-vacuity).
    const MmConfig env = mmEnvConfig(20.0f, 300.0f, 1.0f);
    const float restart = mmRestartObservable(kMmFs, env);
    INFO("restart observable " << restart);
    REQUIRE(restart >= 0.0f);
    REQUIRE(restart < 0.5f);

    SECTION("(a) MONO Legato on, three overlapping notes") {
        VE::VoiceEngineScript s = mmBareScript(MmVoiceMode::Mono, 1, kMmRtN3 + 4800, env);
        s.events.push_back(mmSetIndex(0, MmKind::Legato, 1));
        s.events.push_back(mmOn(kMmRtN1, kMmNoteE1));
        s.events.push_back(mmOn(kMmRtN2, kMmNoteA1));
        s.events.push_back(mmOn(kMmRtN3, kMmNoteC3));
        const VE::VoiceEngineTaps t = VE::voiceEngineRender(s);
        mmCheckRestart(t, kMmRtN1, restart, false);
        mmCheckRestart(t, kMmRtN2, restart, true);
        mmCheckRestart(t, kMmRtN3, restart, true);
    }

    SECTION("(b) MONO High priority: a lower note pressed while a higher one is held") {
        VE::VoiceEngineScript s = mmBareScript(MmVoiceMode::Mono, 1, kMmRtN2 + 4800, env);
        s.events.push_back(mmSetIndex(0, MmKind::MonoPriority, static_cast<int>(MonoMode::HighNote)));
        s.events.push_back(mmOn(kMmRtN1, kMmNoteC3));
        s.events.push_back(mmOn(kMmRtN2, kMmNoteE1));   // lower: the handler keeps C3 sounding
        const VE::VoiceEngineTaps t = VE::voiceEngineRender(s);
        mmCheckRestart(t, kMmRtN1, restart, false);
        mmCheckRestart(t, kMmRtN2, restart, true);
    }

    SECTION("(c) POLY4: a 3-note chord at one sample is one note-on") {
        const auto render = [](const MmConfig& c, std::initializer_list<MmChordNote> chord) {
            VE::VoiceEngineScript s = mmBareScript(MmVoiceMode::Poly, 4, kMmRtN3 + 4800, c);
            s.events.push_back(mmOn(0, kMmNoteE2));   // held before, so the chord restarts from s > 0.5
            for (const MmChordNote& n : chord)
                s.events.push_back(mmOn(kMmRtN2, n.note, n.velocity));
            return VE::voiceEngineRender(s);
        };
        const VE::VoiceEngineTaps chordEnv = render(env, {{.note=kMmNoteE1, .velocity=40}, {.note=kMmNoteA1, .velocity=80}, {.note=kMmNoteC3, .velocity=110}});
        const VE::VoiceEngineTaps singleEnv = render(env, {{.note=kMmNoteE1, .velocity=100}});
        mmCheckRestart(chordEnv, kMmRtN2, restart, true);
        const double dEnv = mmMaxAbsDiff(mmSourceTrace(chordEnv), mmSourceTrace(singleEnv));
        INFO("Envelope: chord vs single note-on, max |ds| " << dEnv);
        CHECK(dEnv <= kMmTraceTol);

        const MmConfig vel = mmSourceConfig(MmSource::Velocity);
        const VE::VoiceEngineTaps chordVel = render(vel, {{.note=kMmNoteE1, .velocity=40}, {.note=kMmNoteA1, .velocity=80}, {.note=kMmNoteC3, .velocity=110}});
        const VE::VoiceEngineTaps singleVel = render(vel, {{.note=kMmNoteE1, .velocity=110}});
        const float last = chordVel.grid[mmGridIndex(chordVel, kMmRtN2)].morphModSource;
        INFO("Velocity: the chord's grid point reads " << last);
        CHECK(std::fabs(static_cast<double>(last) - 110.0 / 127.0) <= kMmTraceTol);   // the last note's velocity
        const double dVel = mmMaxAbsDiff(mmSourceTrace(chordVel), mmSourceTrace(singleVel));
        INFO("Velocity: chord vs single note-on at v 110, max |ds| " << dVel);
        CHECK(dVel <= kMmTraceTol);
    }

    SECTION("(d) POLY4: three staggered notes") {
        VE::VoiceEngineScript s = mmBareScript(MmVoiceMode::Poly, 4, kMmRtN3 + 4800, env);
        s.events.push_back(mmOn(kMmRtN1, kMmNoteE1));
        s.events.push_back(mmOn(kMmRtN2, kMmNoteA1));
        s.events.push_back(mmOn(kMmRtN3, kMmNoteC3));
        const VE::VoiceEngineTaps t = VE::voiceEngineRender(s);
        mmCheckRestart(t, kMmRtN1, restart, false);
        mmCheckRestart(t, kMmRtN2, restart, true);
        mmCheckRestart(t, kMmRtN3, restart, true);
    }

    SECTION("(e) a note-off changes no morphModSource value") {
        const MmConfig c = mmEnvConfig(20.0f, 300.0f, 0.5f);
        for (const MmModeArm& arm : kMmModeArms) {
            INFO(arm.name);
            const auto render = [&](bool addOff) {
                VE::VoiceEngineScript s = mmBareScript(arm.mode, arm.polyphony, 24000, c);
                s.events.push_back(mmOn(0, kMmNoteE1));
                if (addOff)
                    s.events.push_back(mmOff(9600, kMmNoteE1));
                return VE::voiceEngineRender(s);
            };
            const std::vector<float> withOff = mmSourceTrace(render(true));
            const std::vector<float> without = mmSourceTrace(render(false));
            const double d = mmMaxAbsDiff(withOff, without);
            INFO("max |ds| with vs without the note-off " << d);
            CHECK(d <= kMmTraceTol);
            // Non-vacuity: a gated release would have pulled s toward 0; the contour holds near Sustain 0.5.
            REQUIRE(!withOff.empty());
            CHECK(withOff.back() > 0.4f);
        }
    }

    SECTION("(g) a MONO <-> POLY switch with notes held changes no morphModSource value") {
        MmConfig cyc = mmSourceConfig(MmSource::Cycle);
        cyc.cycleShape = Waveform::Sine;
        cyc.cycleRateHz = 3.0f;
        for (const MmConfig& c : {env, cyc}) {
            INFO("source " << mmSourceName(c.source));
            const auto render = [&](bool withSwitch) {
                VE::VoiceEngineScript s = mmBareScript(MmVoiceMode::Mono, 4, 28800, c);
                s.events.push_back(mmOn(0, kMmNoteE1));
                s.events.push_back(mmOn(2400, kMmNoteA1));
                if (withSwitch) {
                    s.events.push_back(mmSetIndex(9600, MmKind::VoiceMode, static_cast<int>(MmVoiceMode::Poly)));
                    s.events.push_back(mmSetIndex(19200, MmKind::VoiceMode, static_cast<int>(MmVoiceMode::Mono)));
                }
                return VE::voiceEngineRender(s);
            };
            const VE::VoiceEngineTaps sw = render(true);
            const VE::VoiceEngineTaps no = render(false);
            const double d = mmMaxAbsDiff(mmSourceTrace(sw), mmSourceTrace(no));
            INFO("max |ds| with vs without the switches " << d);
            CHECK(d <= kMmTraceTol);
            // Non-vacuity: the switches release the held voices, so the sound differs.
            CHECK_FALSE((mmIdentical(sw.outL, no.outL) && mmIdentical(sw.outR, no.outR)));
        }
    }

    SECTION("(h) Sustain 1 held, note-off, 5 s without a note: s stays at S; one global resolved position") {
        const MmConfig c = mmEnvConfig(20.0f, 300.0f, 1.0f);
        constexpr std::uint64_t kOff = 24000;
        constexpr std::uint64_t kTotal = kOff + 5 * 48000;
        const std::array<std::uint8_t, 3> notes{kMmNoteE1, kMmNoteA1, kMmNoteC3};
        for (const MmModeArm& arm : kMmModeArms) {
            INFO(arm.name);
            const bool poly = arm.mode == MmVoiceMode::Poly;
            const std::size_t held = poly ? notes.size() : 1u;
            VE::VoiceEngineScript s =
                mmBareScript(arm.mode, arm.polyphony, kTotal, c, 0.5f, poly ? arm.polyphony : 0u);
            for (std::size_t i = 0; i < held; ++i) {
                s.events.push_back(mmOn(0, notes[i]));
                s.events.push_back(mmOff(kOff, notes[i]));
            }
            const VE::VoiceEngineTaps t = VE::voiceEngineRender(s);
            const std::size_t kO = mmGridIndex(t, kOff);
            REQUIRE(kO > 0u);
            INFO("s before the note-off " << t.grid[kO - 1].morphModSource);
            CHECK(std::fabs(static_cast<double>(t.grid[kO - 1].morphModSource) - 1.0) <= kMmTraceTol);   // in Sustain
            std::size_t away = 0;
            double worst = 0.0;
            for (std::size_t k = kO; k < t.grid.size(); ++k) {
                const double d = std::fabs(static_cast<double>(t.grid[k].morphModSource) - 1.0);
                worst = std::max(worst, d);
                if (d > kMmTraceTol)
                    ++away;
            }
            INFO("worst |s - S| from the note-off on " << worst);
            CHECK(away == 0u);
            CHECK(t.grid.size() - kO >= static_cast<std::size_t>(5 * 48000 / kMmGrid));

            if (poly) {
                // One global value: one grid record per interval, and no voice chunk straddles two grid points, so
                // every voice rendering in an interval reads that interval's single resolved position.
                std::size_t misplaced = 0;
                for (std::size_t k = 0; k < t.grid.size(); ++k) {
                    if (t.grid[k].sample != static_cast<std::uint64_t>(k) * kMmGrid)
                        ++misplaced;
                }
                CHECK(misplaced == 0u);
                std::vector<unsigned> voicesIn(t.grid.size(), 0u);
                std::size_t straddling = 0;
                std::size_t orphan = 0;
                std::size_t voicesWithChunks = 0;
                for (std::size_t v = 0; v < t.tappedVoices; ++v) {
                    const std::vector<VE::VoiceEngineChunkRecord>& chunks = t.voices[v].chunks;
                    if (!chunks.empty())
                        ++voicesWithChunks;
                    for (const VE::VoiceEngineChunkRecord& ch : chunks) {
                        if (ch.n == 0u)
                            continue;
                        const std::uint64_t kFirst = ch.sample / kMmGrid;
                        const std::uint64_t kLast = (ch.sample + ch.n - 1u) / kMmGrid;
                        if (kFirst != kLast)
                            ++straddling;
                        if (kFirst >= t.grid.size()) {
                            ++orphan;
                            continue;
                        }
                        voicesIn[static_cast<std::size_t>(kFirst)] |= (1u << v);
                    }
                }
                std::size_t shared = 0;
                for (const unsigned m : voicesIn) {
                    if (std::popcount(m) >= 3)
                        ++shared;
                }
                INFO("voices with chunks " << voicesWithChunks << ", intervals with >= 3 voices " << shared);
                CHECK(straddling == 0u);
                CHECK(orphan == 0u);
                CHECK(voicesWithChunks >= held);
                CHECK(shared >= 1u);   // non-vacuity: three voices render in the same intervals
            }
        }
    }
}

// ------------------------------------------------------------------------------
// SC-012 (b)(c)(c'), SC-003 (d) engine half, SC-013 (e) engine half and (h): lifecycle
// ------------------------------------------------------------------------------

TEST_CASE("ProfundumEngine_MorphModLifecycle", "[systems][profundum][morphmod][voice-engine]") {
    enableFTZDAZ();

    SECTION("(b) same seed renders identically; (SC-003 d) S&H / Smooth Random do not follow the engine seed") {
        const auto cycleRender = [](Waveform shape, std::uint32_t seed) {
            MmConfig c = mmSourceConfig(MmSource::Cycle);
            c.cycleShape = shape;
            c.cycleRateHz = 4.0f;
            VE::VoiceEngineScript s = mmBareScript(MmVoiceMode::Mono, 1, 48000, c);
            s.seed = seed;
            s.events.push_back(mmOn(0, kMmNoteE1));
            return VE::voiceEngineRender(s);
        };
        const VE::VoiceEngineTaps r1 = cycleRender(Waveform::SampleHold, ProfundumEngine::kDefaultSeed);
        const VE::VoiceEngineTaps r2 = cycleRender(Waveform::SampleHold, ProfundumEngine::kDefaultSeed);
        CHECK(mmIdentical(r1.outL, r2.outL));
        CHECK(mmIdentical(r1.outR, r2.outR));
        CHECK(mmIdentical(mmSourceTrace(r1), mmSourceTrace(r2)));

        for (const Waveform shape : {Waveform::SampleHold, Waveform::SmoothRandom}) {
            INFO("shape " << static_cast<int>(shape));
            const std::vector<float> seedOne = mmSourceTrace(cycleRender(shape, 1u));
            const std::vector<float> seedDefault =
                shape == Waveform::SampleHold ? mmSourceTrace(r1)
                                              : mmSourceTrace(cycleRender(shape, ProfundumEngine::kDefaultSeed));
            CHECK(mmIdentical(seedOne, seedDefault));
            // Non-vacuity: the trace moves (several distinct values).
            std::vector<float> distinct = seedOne;
            std::sort(distinct.begin(), distinct.end());
            distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
            INFO("distinct values " << distinct.size());
            CHECK(distinct.size() >= 3u);
        }
    }

    SECTION("(c) prepare keeps the pending config; reset zeroes Envelope and Velocity, keeps wheel and config") {
        MmConfig cfg{};
        cfg.source = MmSource::Envelope;
        cfg.amount = -0.6f;
        cfg.envAttackMs = 50.0f;
        cfg.envDecayMs = 900.0f;
        cfg.envSustain = 0.4f;
        cfg.cycleShape = Waveform::Triangle;
        cfg.cycleRateHz = 3.0f;
        cfg.cycleSync = true;
        cfg.cycleNote = NoteValue::Eighth;
        cfg.cycleModifier = NoteModifier::Dotted;
        {
            auto e = std::make_unique<ProfundumEngine>();
            e->prepare(kMmFs);
            e->setMorphModulation(cfg);
            e->setMorphModWheel(0.7f);
            e->prepare(96000.0);
            CHECK(mmConfigFieldEqual(e->morphModulation(), cfg));
            CHECK(e->morphModWheel() == 0.7f);
        }

        for (const MmSource src : {MmSource::Envelope, MmSource::Velocity}) {
            INFO("source " << mmSourceName(src));
            MmConfig c = mmEnvConfig(400.0f, 300.0f, 1.0f);
            c.source = src;
            std::vector<MmGridPoint> grid;
            grid.reserve(256);
            MmGridObserver obs(grid);
            auto e = mmMakeEngine(c, 0.25f);
            e->setMorphModWheel(0.7f);
            e->setObserver(&obs);
            e->prepare(kMmFs);
            e->noteOn(kMmNoteE1, 100);
            mmRenderBlocks(*e, 20);
            REQUIRE(!grid.empty());
            INFO("s before reset " << grid.back().source);
            CHECK(grid.back().source > 0.0f);   // non-vacuity
            (*e).reset();
            const std::size_t n0 = grid.size();
            mmRenderBlocks(*e, 1);
            REQUIRE(grid.size() > n0);
            CHECK(grid[n0].sample == 0u);
            CHECK(grid[n0].source == 0.0f);
            CHECK(e->morphModWheel() == 0.7f);
            CHECK(mmConfigFieldEqual(e->morphModulation(), c));
            e->setObserver(nullptr);
        }
    }

    SECTION("(c') the host tempo reaches a synced Cycle") {
        const NoteValueMapping m = kNoteValueDropdownMapping[13];   // 1/4
        const double beats = static_cast<double>(getBeatsForNote(m.note, m.modifier));
        REQUIRE(beats == 1.0);
        MmConfig c = mmSourceConfig(MmSource::Cycle);
        c.cycleShape = Waveform::Sine;
        c.cycleSync = true;
        c.cycleNote = m.note;
        c.cycleModifier = m.modifier;
        for (const float bpm : {90.0f, 120.0f}) {
            const double expected = beats * 60.0 / static_cast<double>(bpm);   // 0.6667 s, 0.5 s
            INFO(bpm << " BPM, expected period " << expected << " s");
            const auto total = static_cast<std::uint64_t>(std::ceil(11.5 * expected * kMmFs));
            VE::VoiceEngineScript s = mmBareScript(MmVoiceMode::Mono, 1, total, c);
            s.events.push_back(mmSetValue(0, MmKind::Tempo, bpm));   // before the first block
            s.events.push_back(mmOn(0, kMmNoteE1));
            const VE::VoiceEngineTaps t = VE::voiceEngineRender(s);
            const MmPeriod p = mmUpwardCrossingPeriod(mmSourceTrace(t), kMmFs / static_cast<double>(kMmGrid));
            INFO("measured period " << p.seconds << " s over " << p.cycles << " cycles");
            CHECK(p.cycles >= 10u);
            CHECK(std::fabs(p.seconds - expected) / expected <= 0.005);
        }
    }

    SECTION("(SC-013 e) the runtime blob carries b; a real BassMorphEngine restores it") {
        const MmConfig c = mmEnvConfig(400.0f, 300.0f, 1.0f);
        std::vector<MmGridPoint> grid;
        grid.reserve(256);
        MmGridObserver obs(grid);
        auto e = mmMakeEngine(c, 0.25f);
        e->setObserver(&obs);
        e->prepare(kMmFs);
        e->noteOn(kMmNoteE1, 100);
        mmRenderBlocks(*e, 40);
        REQUIRE(!grid.empty());
        INFO("s " << grid.back().source << ", modulated target " << e->morphPosition());
        CHECK(grid.back().source > 0.0f);
        CHECK_FALSE(mmSame(e->morphPosition(), 0.25f));   // non-vacuity: the target handed to the morph is not b
        CHECK(mmSame(e->morphBasePosition(), 0.25f));

        std::array<std::byte, BassMorphEngine::kMorphRuntimeBytes> blob{};
        REQUIRE(e->serializeMorphRuntime(blob.data(), blob.size()) == blob.size());
        float stored = -1.0f;
        std::memcpy(&stored, blob.data() + 1, sizeof(float));
        CHECK(std::bit_cast<std::uint32_t>(stored) == std::bit_cast<std::uint32_t>(0.25f));

        auto fresh = std::make_unique<BassMorphEngine>();
        REQUIRE(fresh->setEndpoint(BassMorphEngine::Endpoint::A, auRd::auStateAs()));
        REQUIRE(fresh->setEndpoint(BassMorphEngine::Endpoint::B, auRd::auStateB()));
        REQUIRE(fresh->restoreRuntime(blob.data(), blob.size()));
        CHECK(mmSame(fresh->targetPosition(), 0.25f));
        e->setObserver(nullptr);
    }

    SECTION("(SC-013 h) a mid-stream restore resets the modulator") {
        std::array<std::byte, BassMorphEngine::kMorphRuntimeBytes> blob{};
        {
            auto donor = mmMakeEngine(MmConfig{}, 0.75f);   // Source Off
            donor->prepare(kMmFs);
            REQUIRE(donor->serializeMorphRuntime(blob.data(), blob.size()) == blob.size());
        }
        float blobBase = -1.0f;
        std::memcpy(&blobBase, blob.data() + 1, sizeof(float));
        REQUIRE(mmSame(blobBase, 0.75f));

        MmConfig cycle = mmSourceConfig(MmSource::Cycle);
        cycle.cycleShape = Waveform::Sine;
        cycle.cycleRateHz = 1.0f;
        struct RestoreArm {
            MmConfig config;
            bool note;
            const char* what;
        };
        const std::array<RestoreArm, 3> restoreArms{
            RestoreArm{.config=mmSourceConfig(MmSource::Velocity), .note=true, .what="Velocity, note v 100"},
            RestoreArm{.config=mmEnvConfig(400.0f, 300.0f, 1.0f), .note=true, .what="Envelope A 400 ms, no note-on after the restore"},
            RestoreArm{.config=cycle, .note=false, .what="Cycle 1 Hz Sine, free"}};
        for (const RestoreArm& arm : restoreArms) {
            INFO(arm.what);
            std::vector<MmGridPoint> grid;
            grid.reserve(256);
            MmGridObserver obs(grid);
            auto e = mmMakeEngine(arm.config, 0.25f);
            e->setObserver(&obs);
            e->prepare(kMmFs);
            if (arm.note)
                e->noteOn(kMmNoteE1, 100);
            if (arm.config.source == MmSource::Velocity) {
                bool reached = false;
                for (std::size_t b = 0; b < 16 && !reached; ++b) {
                    mmRenderBlocks(*e, 1);
                    reached = !grid.empty()
                              && std::fabs(static_cast<double>(grid.back().source) - 100.0 / 127.0) <= kMmTraceTol;
                }
                REQUIRE(reached);
            } else {
                mmRenderBlocks(*e, 40);
            }
            REQUIRE(!grid.empty());
            const float before = grid.back().source;
            INFO("s before the restore " << before);
            CHECK(before > 0.0f);

            REQUIRE(e->restoreMorphRuntime(blob.data(), blob.size()));   // mid-stream: blocks have rendered
            CHECK(mmSame(e->morphBasePosition(), blobBase));
            const std::size_t n0 = grid.size();
            mmRenderBlocks(*e, 1);
            REQUIRE(grid.size() > n0);
            const float after = grid[n0].source;
            INFO("s at the next grid point " << after);
            if (arm.config.source == MmSource::Cycle) {
                ProfundumMorphModulator fresh;
                fresh.prepare(kMmFs);
                fresh.setConfig(arm.config);
                const float expected = fresh.advanceInterval();
                INFO("a freshly prepared modulator's first interval " << expected);
                CHECK(std::fabs(static_cast<double>(after) - static_cast<double>(expected)) <= kMmTraceTol);
                CHECK(std::fabs(static_cast<double>(before) - static_cast<double>(expected)) > 0.1);   // non-vacuity
            } else {
                CHECK(after == 0.0f);
            }
            e->setObserver(nullptr);
        }
    }
}

// ------------------------------------------------------------------------------
// SC-019 (engine): the Anchor path stays bit-identical L/R while the modulation sweeps Solid <-> Wide
// ------------------------------------------------------------------------------

TEST_CASE("ProfundumEngine_MorphModAnchorMono", "[systems][profundum][morphmod][voice-engine]") {
    enableFTZDAZ();

    MorphState solid = auRd::auStateAs();
    solid.field.mode = BassStereoField::ImageMode::Solid;
    MorphState wide = auRd::auStateB();
    wide.field.mode = BassStereoField::ImageMode::Wide;
    REQUIRE(isValidMorphState(solid));
    REQUIRE(isValidMorphState(wide));

    MmConfig c = mmSourceConfig(MmSource::Cycle);
    c.cycleShape = Waveform::Sine;
    c.cycleRateHz = 1.0f;
    constexpr std::uint64_t kTotal = 144000;   // 3 s
    const std::array<std::uint8_t, 3> notes{kMmNoteE1, kMmNoteA1, kMmNoteC3};

    for (const MmModeArm& arm : kMmModeArms) {
        INFO(arm.name);
        const bool poly = arm.mode == MmVoiceMode::Poly;
        const std::size_t played = poly ? notes.size() : 1u;
        VE::VoiceEngineScript s = mmBareScript(arm.mode, arm.polyphony, kTotal, c, 0.5f, arm.polyphony);
        s.a = solid;
        s.b = wide;
        for (std::size_t i = 0; i < played; ++i)
            s.events.push_back(mmOn(0, notes[i]));
        const VE::VoiceEngineTaps t = VE::voiceEngineRender(s);

        std::size_t sounding = 0;
        for (std::size_t v = 0; v < t.tappedVoices; ++v) {
            const VE::VoiceEngineVoiceTapSet& p = t.voices[v];
            REQUIRE(p.anchorL.size() == p.anchorR.size());
            std::size_t differ = 0;
            std::size_t nonZero = 0;
            for (std::size_t n = 0; n < p.anchorL.size(); ++n) {
                if (!mmSame(p.anchorL[n], p.anchorR[n]))
                    ++differ;
                if (p.anchorL[n] != 0.0f)
                    ++nonZero;
            }
            INFO("voice " << v << ": samples with anchorL != anchorR " << differ << ", non-zero " << nonZero);
            CHECK(differ == 0u);
            if (nonZero > 0)
                ++sounding;
        }
        CHECK(sounding >= played);   // non-vacuity: the voices' Anchor taps carry signal
        CHECK(mmIdentical(t.anchorL, t.anchorR));

        // Non-vacuity: the resolved position spans >= 0.9 of [0, 1].
        REQUIRE(!t.grid.empty());
        float lo = t.grid.front().resolved.position;
        float hi = lo;
        for (const VE::VoiceEngineGridRecord& r : t.grid) {
            lo = std::min(lo, r.resolved.position);
            hi = std::max(hi, r.resolved.position);
        }
        INFO("resolved position span [" << lo << ", " << hi << "]");
        CHECK(static_cast<double>(hi) - static_cast<double>(lo) >= 0.9);
    }
}

// ==============================================================================
// T009: SC-011 click bound (FR-011, FR-012, C-9), sharded (plan 6, plan 8 "Shard sizing")
// ==============================================================================
// Showcase pair A_s / B, E1, MONO, 48 kHz, host block 64, b 0.5, 4 s per arm. The fixed arm list is 18 base arms
// (Envelope (A, D) in {0.1, 10 000}^2 ms at S 0; Cycle {0.01, 20} Hz x the six shapes; Velocity with notes at v 1 and
// 127 alternating every 0.5 s; Mod Wheel with MorphModWheel events 0 <-> 1 every 0.5 s), each x A in {-1, +1} x
// ramp in {5, 2000} ms: index = base * 4 + amountIndex * 2 + rampIndex, and shard k (TEST_CASE _<k+1>of8) runs the
// arms with index % 8 == k, so every shard carries every other base arm (base % 2 == k / 4) at one (A, ramp) pair.
// Per arm: (a) every interval |dp| within the SC-005 (d) bar; (b) max |y[n] - y[n-1]| over outL and outR <= 2 x the
// max of the same metric on two static renders (Source Off, the same notes) at the trajectory's recorded min and max
// resolved position (the Phase 13 FR-048 factor-2 rule; static renders cached by position bits and note script);
// (c) 0 non-finite samples (detail::isFinite). Each shard prints its wall clock (WARN); it must read <= 9 s (60 % of
// the 15 s per-push bar) on the dev box, recorded by the G8 gate, never asserted here (a concurrent lane would make
// it a false red). The four-shard split read 11.1-13.9 s alone (2026-10-27, tasks.md Defects found), so the arms are
// re-sharded into 8, never shortened.

namespace {

constexpr std::uint64_t kMmClickSamples = 192000;   // 4 s
constexpr std::uint64_t kMmClickToggle = 24000;     // 0.5 s: the Velocity notes and the Mod Wheel events
constexpr float kMmClickBase = 0.5f;
constexpr std::size_t kMmClickShards = 8;
constexpr std::size_t kMmClickBaseArms = 18;
constexpr std::array<float, 2> kMmClickAmounts{-1.0f, 1.0f};
constexpr std::array<float, 2> kMmClickRampsMs{5.0f, 2000.0f};
constexpr double kMmClickFactor = 2.0;   // the Phase 13 FR-048 factor-2 rule

enum class MmClickNotes : std::uint8_t {
    Held,                  ///< auScript's E1 at sample 0, held for the whole render
    VelocityAlternating,   ///< E1 retriggered every 0.5 s, v 1 and 127 alternating (note-off then note-on)
};

struct MmClickArm {
    MmConfig config{};
    float rampMs = 5.0f;
    bool wheelToggles = false;
    MmClickNotes notes = MmClickNotes::Held;
};

/// The fixed arm list (72 arms), in the order the header comment states.
[[nodiscard]] std::vector<MmClickArm> mmClickArms() {
    std::vector<MmClickArm> bases;
    for (const float attackMs : {0.1f, 10000.0f}) {
        for (const float decayMs : {0.1f, 10000.0f})
            bases.push_back(MmClickArm{.config = mmEnvConfig(attackMs, decayMs, 0.0f)});
    }
    for (const float hz : {0.01f, 20.0f}) {
        for (const Waveform w : {Waveform::Sine, Waveform::Triangle, Waveform::Sawtooth, Waveform::Square,
                                 Waveform::SampleHold, Waveform::SmoothRandom}) {
            MmConfig c = mmSourceConfig(MmSource::Cycle);
            c.cycleShape = w;
            c.cycleRateHz = hz;
            bases.push_back(MmClickArm{.config = c});
        }
    }
    bases.push_back(
        MmClickArm{.config = mmSourceConfig(MmSource::Velocity), .notes = MmClickNotes::VelocityAlternating});
    bases.push_back(MmClickArm{.config = mmSourceConfig(MmSource::ModWheel), .wheelToggles = true});
    REQUIRE(bases.size() == kMmClickBaseArms);

    std::vector<MmClickArm> arms;
    arms.reserve(bases.size() * kMmClickAmounts.size() * kMmClickRampsMs.size());
    for (const MmClickArm& base : bases) {
        for (const float amount : kMmClickAmounts) {
            for (const float rampMs : kMmClickRampsMs) {
                MmClickArm a = base;
                a.config.amount = amount;
                a.rampMs = rampMs;
                arms.push_back(a);
            }
        }
    }
    return arms;
}

/// Replaces auScript's held note with the Velocity arm's alternating notes; Held keeps it.
void mmClickNoteEvents(std::vector<VE::VoiceEngineEvent>& events, MmClickNotes notes) {
    if (notes == MmClickNotes::Held)
        return;
    std::erase_if(events, [](const VE::VoiceEngineEvent& e) {
        return e.kind == MmKind::NoteOn || e.kind == MmKind::NoteOff;
    });
    std::uint64_t i = 0;
    for (std::uint64_t at = 0; at < kMmClickSamples; at += kMmClickToggle, ++i) {
        if (at > 0)
            events.push_back(mmOff(at, kMmNoteE1));   // pushed first: the stable sort keeps the off before the on
        events.push_back(mmOn(at, kMmNoteE1, i % 2 == 0 ? std::uint8_t{1} : std::uint8_t{127}));
    }
}

/// The modulated render: the observer attached (grid records), no per-voice taps.
[[nodiscard]] VE::VoiceEngineScript mmClickScript(const MmClickArm& arm) {
    VE::VoiceEngineScript s = mmCompositionScript(kMmClickBase, arm.rampMs, arm.config, 0.0f, false, kMmClickSamples);
    REQUIRE(s.tappedVoices == 0u);
    mmClickNoteEvents(s.events, arm.notes);
    if (arm.wheelToggles) {
        std::uint64_t i = 0;
        for (std::uint64_t at = kMmClickToggle; at < kMmClickSamples; at += kMmClickToggle, ++i)
            s.events.push_back(mmSetValue(at, MmKind::MorphModWheel, i % 2 == 0 ? 1.0f : 0.0f));
    }
    return s;
}

/// The static render at position p: Source Off, the same notes, the output only.
[[nodiscard]] VE::VoiceEngineTaps mmClickStatic(float p, MmClickNotes notes) {
    VE::VoiceEngineScript s = mmCompositionScript(p, 5.0f, MmConfig{}, 0.0f, false, kMmClickSamples);
    s.attachObserver = false;
    mmClickNoteEvents(s.events, notes);
    return VE::voiceEngineRender(s);
}

/// max over n >= 1, ch in {L, R} of |y[n] - y[n-1]| (the FR-048 step metric, preset_rt_test.cpp rtStep).
[[nodiscard]] double mmMaxStep(const std::vector<float>& l, const std::vector<float>& r) {
    REQUIRE(l.size() == r.size());
    double worst = 0.0;
    for (std::size_t n = 1; n < l.size(); ++n) {
        worst = std::max(worst, std::fabs(static_cast<double>(l[n]) - static_cast<double>(l[n - 1])));
        worst = std::max(worst, std::fabs(static_cast<double>(r[n]) - static_cast<double>(r[n - 1])));
    }
    return worst;
}

[[nodiscard]] std::size_t mmNonFinite(const std::vector<float>& x) noexcept {
    std::size_t n = 0;
    for (const float v : x) {
        if (!detail::isFinite(v))
            ++n;
    }
    return n;
}

void mmRunClickShard(std::size_t k) {
    enableFTZDAZ();
    REQUIRE(k < kMmClickShards);
    const auto t0 = std::chrono::steady_clock::now();

    const std::vector<MmClickArm> arms = mmClickArms();
    REQUIRE(arms.size() == kMmClickBaseArms * kMmClickAmounts.size() * kMmClickRampsMs.size());

    std::map<std::pair<std::uint32_t, MmClickNotes>, double> staticSteps;
    const auto staticStep = [&staticSteps](float p, MmClickNotes notes) {
        const auto key = std::make_pair(std::bit_cast<std::uint32_t>(p), notes);
        if (const auto it = staticSteps.find(key); it != staticSteps.end())
            return it->second;
        const VE::VoiceEngineTaps st = mmClickStatic(p, notes);
        const double step = mmMaxStep(st.outL, st.outR);
        staticSteps.emplace(key, step);
        return step;
    };

    std::size_t ran = 0;
    double worstRatio = 0.0;
    for (std::size_t i = 0; i < arms.size(); ++i) {
        if (i % kMmClickShards != k)
            continue;
        const MmClickArm& arm = arms[i];
        INFO("arm " << i << ": source " << mmSourceName(arm.config.source) << ", A " << arm.config.amount
                    << ", ramp " << arm.rampMs << " ms, env A " << arm.config.envAttackMs << " / D "
                    << arm.config.envDecayMs << " ms, Cycle shape " << static_cast<int>(arm.config.cycleShape)
                    << " at " << arm.config.cycleRateHz << " Hz");
        const VE::VoiceEngineTaps t = VE::voiceEngineRender(mmClickScript(arm));
        REQUIRE(t.grid.size() >= static_cast<std::size_t>(kMmClickSamples / kMmGrid));

        // (a) the SC-005 (d) per-interval bar; the recorded min and max resolved position.
        const double dMax = mmDeltaMax(static_cast<double>(arm.rampMs), kMmFs);
        std::size_t stepViolations = 0;
        double worstStepRatio = 0.0;
        float prev = kMmClickBase;   // prepare snaps p to the base (P-1)
        float lo = t.grid.front().resolved.position;
        float hi = lo;
        for (const VE::VoiceEngineGridRecord& r : t.grid) {
            const float p = r.resolved.position;
            const double dp = std::fabs(static_cast<double>(p) - static_cast<double>(prev));
            worstStepRatio = std::max(worstStepRatio, dp / dMax);
            if (dp > mmStepBar(dMax, prev, p))
                ++stepViolations;
            lo = std::min(lo, p);
            hi = std::max(hi, p);
            prev = p;
        }

        // (b) the factor-2 rule against the static renders at lo and hi.
        const double armStep = mmMaxStep(t.outL, t.outR);
        const double staticLo = staticStep(lo, arm.notes);
        const double staticHi = staticStep(hi, arm.notes);
        const double reference = std::max(staticLo, staticHi);

        // (c) non-finite samples.
        const std::size_t nonFinite = mmNonFinite(t.outL) + mmNonFinite(t.outR);

        INFO("resolved position [" << lo << ", " << hi << "], worst |dp| / dp_max " << worstStepRatio
                                   << "; max step " << armStep << " vs static " << staticLo << " (p " << lo
                                   << "), " << staticHi << " (p " << hi << "); non-finite " << nonFinite);
        CHECK(stepViolations == 0u);
        REQUIRE(reference > 0.0);   // non-vacuity: the static renders carry signal
        worstRatio = std::max(worstRatio, armStep / reference);
        CHECK(armStep <= kMmClickFactor * reference);
        CHECK(nonFinite == 0u);
        ++ran;
    }
    CHECK(ran == arms.size() / kMmClickShards);

    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    WARN("SC-011 shard " << (k + 1) << " of " << kMmClickShards << ": " << ran << " arms, " << staticSteps.size()
                         << " static renders, worst step / static " << worstRatio << " (bar " << kMmClickFactor
                         << "), wall clock " << wall << " s (bar 9 s)");
}

}  // namespace

TEST_CASE("ProfundumEngine_MorphModClickFree_1of8", "[systems][profundum][morphmod][voice-engine]") {
    mmRunClickShard(0);
}

TEST_CASE("ProfundumEngine_MorphModClickFree_2of8", "[systems][profundum][morphmod][voice-engine]") {
    mmRunClickShard(1);
}

TEST_CASE("ProfundumEngine_MorphModClickFree_3of8", "[systems][profundum][morphmod][voice-engine]") {
    mmRunClickShard(2);
}

TEST_CASE("ProfundumEngine_MorphModClickFree_4of8", "[systems][profundum][morphmod][voice-engine]") {
    mmRunClickShard(3);
}

TEST_CASE("ProfundumEngine_MorphModClickFree_5of8", "[systems][profundum][morphmod][voice-engine]") {
    mmRunClickShard(4);
}

TEST_CASE("ProfundumEngine_MorphModClickFree_6of8", "[systems][profundum][morphmod][voice-engine]") {
    mmRunClickShard(5);
}

TEST_CASE("ProfundumEngine_MorphModClickFree_7of8", "[systems][profundum][morphmod][voice-engine]") {
    mmRunClickShard(6);
}

TEST_CASE("ProfundumEngine_MorphModClickFree_8of8", "[systems][profundum][morphmod][voice-engine]") {
    mmRunClickShard(7);
}
