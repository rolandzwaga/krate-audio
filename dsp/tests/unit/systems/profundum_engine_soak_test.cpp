// ==============================================================================
// Profundum Phase 9 (specs/profundum-phase9-voice-engine) - profundum_engine_soak_test.cpp
// ==============================================================================
// SC-013 (30 min soak: finite, true peak, DC, state finite) and SC-014 (Anchor-h1 lock inside the soak, H-6);
// FR-061, FR-062; H-2 recorded. tasks.md T019 (plan S10.3 / S10.6 rows SC-013, SC-014; R-E, R-K).
// Decisions: evidence/run-log/T019.txt.
//
// Cases:
//   VoiceEngine_Soak30Min_Mono  [long]  15 min MONO, seed 0x5013u  -> evidence/soak-mono.txt
//   VoiceEngine_Soak30Min_Poly  [long]  15 min POLY, seed 0x5014u  -> evidence/soak-poly.txt
//   VoiceEngine_SoakSmoke               20 s MONO, one 15 s lock segment (Depth), one window -> evidence/soak-smoke.txt
//
// The render is streamed (R-K): the observer never copies every sample. It checks every FR-086 tap with
// detail::isFinite in place and copies only the per-voice phase windows of the lock segments (Anchor, core,
// Character and f_core taps); the output is checked block by block (finite, test-side 4x halfband true peak,
// BH7-weighted 10 s DC).
// ==============================================================================

#include <krate/dsp/systems/profundum_engine.h>
#include <krate/dsp/systems/profundum_engine_observer.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>
#include <krate/dsp/systems/profundum_motion.h>
#include <krate/dsp/systems/profundum_voice.h>

#include "bass_morph_state_helpers.h"
#include "profundum_voice_engine_test_helpers.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/polyphase_halfband_oversampler.h>
#include <krate/dsp/processors/mono_handler.h>
#include <krate/dsp/processors/note_memory.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/anchor_oscillator.h>
#include <krate/dsp/systems/bass_character_stage.h>
#include <krate/dsp/systems/bass_morph_engine.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/mass_law.h>
#include <krate/dsp/systems/morph_state.h>

#include <enable_ftz_daz.h>

#include <catch2/catch_test_macros.hpp>

#include <iostream>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using Krate::DSP::AnchorOscillator;
using Krate::DSP::BassCharacterStage;
using Krate::DSP::BassMorphEngine;
using Krate::DSP::BassStereoField;
using Krate::DSP::GlideMode;
using Krate::DSP::MassLaw;
using Krate::DSP::MonoMode;
using Krate::DSP::MorphState;
using Krate::DSP::NoteMemory;
using Krate::DSP::PolyphaseHalfbandOversampler;
using Krate::DSP::PortaMode;
using Krate::DSP::ProfundumEngine;
using Krate::DSP::ProfundumEngineObserver;
using Krate::DSP::ProfundumEngineTaps;
using Krate::DSP::ProfundumGridRecord;
using Krate::DSP::ProfundumMacro;
using Krate::DSP::ProfundumMacroMatrix;
using Krate::DSP::ProfundumVoiceTaps;
using Krate::DSP::SpectralShapeRecipe;
namespace PT = Krate::DSP::ProfundumTest;
namespace BMT = Krate::DSP::BassMorphTest;

using VeKind = PT::VoiceEngineEventKind;
using VeTarget = MorphState::MotionConfig::Target;
using VeSource = MorphState::MotionConfig::Source;

constexpr double kVeFs = 48000.0;
constexpr std::size_t kVeHostBlock = ProfundumEngine::kControlInterval;   // one poll per interval (SC-013)
constexpr std::size_t kVeMaxWindows = 3;
constexpr double kVeCaptureSec = 1.1;            // >= lockWindowLength (ceil(f0) periods <= 1.05 s for f0 >= 20 Hz)
constexpr double kVeDcWindowSec = 10.0;          // SC-013
constexpr double kVeTrendSec = 300.0;            // SC-013: first / last 5 min
constexpr double kVeDcBarDb = -80.0;             // SC-013
constexpr double kVeTrendFloorDb = kVeDcBarDb - 20.0;   // G14-2 round 5: a "rise" that stays 20 dB under the bar is not build-up
constexpr double kVeTrendBarDb = 1.0;            // SC-013
constexpr double kVeTruePeakMarginDb = 0.1;      // SC-013
constexpr double kVeLockBarDeg = 0.5;            // SC-014 (the Phase 3 SC-003 bar)
constexpr double kVeRadToDeg = 57.295779513082320876798154814105;
constexpr double kVeDbFloor = 1.0e-15;           // log argument floor only (-300 dB); not a bar

// ---- evidence ----

[[nodiscard]] std::string veEvidenceDir() {
    return std::string(KRATE_DSP_TESTS_DIR) + "/../../specs/profundum-phase9-voice-engine/evidence/";
}

void veWriteEvidence(const std::string& relPath, const std::string& text) {
    const std::filesystem::path p = std::filesystem::path(veEvidenceDir()) / relPath;
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p);
    out << text;
    CHECK(out.good());
}

// ---- small helpers ----

[[nodiscard]] double veUnit(std::mt19937& rng) {
    return BMT::unitFromWord(static_cast<std::uint32_t>(rng()));
}

[[nodiscard]] std::uint32_t veWord(std::mt19937& rng) { return static_cast<std::uint32_t>(rng()); }

[[nodiscard]] std::uint64_t veSamples(double sec) {
    return static_cast<std::uint64_t>(std::llround(sec * kVeFs));
}

[[nodiscard]] double veDb(double x) { return 20.0 * std::log10(std::max(std::abs(x), kVeDbFloor)); }

[[nodiscard]] double veWrapDeg(double d) {
    d = std::fmod(d, 360.0);
    if (d > 180.0)
        d -= 360.0;
    else if (d <= -180.0)
        d += 360.0;
    return d;
}

/// round(k fs / f0) samples with k = ceil(f0) periods (>= 1 s): the Phase 3 lock window
/// (profundum_anchor_chain_test.cpp:65, lockWindowLength).
[[nodiscard]] std::size_t veLockWindowLength(double fs, double f0) {
    return static_cast<std::size_t>(std::llround(std::ceil(f0) * fs / f0));
}

/// Every float leaf of a MorphState finite (SC-013: "every float of Resolved").
[[nodiscard]] bool veStateFinite(const MorphState& s) noexcept {
    bool ok = true;
    Krate::DSP::detail::morph_state_io::forEachMorphLeaf(
        [&](auto, const auto& v) {
            if constexpr (std::is_same_v<std::remove_cvref_t<decltype(v)>, float>)
                ok = ok && Krate::DSP::detail::isFinite(v);
        },
        s);
    return ok;
}

[[nodiscard]] std::uint64_t veCountNonFinite(std::span<const float> x, std::size_t n) noexcept {
    std::uint64_t bad = 0;
    const std::size_t m = std::min(x.size(), n);
    for (std::size_t i = 0; i < m; ++i)
        bad += Krate::DSP::detail::isFinite(x[i]) ? 0u : 1u;
    return bad;
}

// ---- the SC-013 state poll (see run-log T019: DEFECT, the engine exposes no poll) ----

struct VeStatePoll {
    bool stateAvailable = false;
    bool oversamplerAvailable = false;
    std::uint64_t polls = 0;
    std::uint64_t stateBad = 0;
    std::uint64_t oversamplerBad = 0;
};

/// SC-013 "per interval the stateFinite() voices / components and oversamplerStateFinite()". Engages as soon as
/// ProfundumEngine declares the polls (T022); until then the result says "unavailable" and the case WARNs.
template <class E>
void vePollEngineState(const E& e, VeStatePoll& p) noexcept {
    ++p.polls;
    if constexpr (requires { static_cast<bool>(e.stateFinite()); }) {
        p.stateAvailable = true;
        if (!static_cast<bool>(e.stateFinite()))
            ++p.stateBad;
    }
    if constexpr (requires { static_cast<bool>(e.oversamplerStateFinite()); }) {
        p.oversamplerAvailable = true;
        if (!static_cast<bool>(e.oversamplerStateFinite()))
            ++p.oversamplerBad;
    }
}

// ---- states ----

/// SC-014 lock state: a held LOCK Sine note with no Drop, the Motion target of the segment (CYCLE 0.5 Hz on Depth /
/// Body, DRIFT on Edge / Shift; amount 1, Motion Depth 1, plan "drafted values"). The processed voice of Phase 3
/// SC-003(e) (Fold 100) so H-2 (the Fold at C3) is recorded from the same segments; the gated measurand is the core
/// tap, ahead of the Fold. Main loop 2026-10-21 (G14-3): the lock segments run at excitation mix 0. The core tap sits
/// after the edge excitation, whose h1 phase rotates with its drive, and the Edge / Shift segments (DRIFT, amount 1)
/// modulate that drive: the first 30 min soak read 4.15 deg (Edge) and 16.65 deg (Shift) while Depth / Body read
/// 0.16 / 0.08 deg. The Anchor's reference is the linear core's phase (Phase 3), so the lock is measured against it;
/// the activity portions keep the hot state (excitation 1 / 1 / 1).
[[nodiscard]] MorphState veLockState(VeTarget t) {
    MorphState s{};
    s.recipe = SpectralShapeRecipe::kDefaultControls;
    s.excitation.mix = 0.0f;   // G14-3: the gated lock reads the linear core (see above)
    s.excitation.drive = 1.0f;
    s.excitation.asymmetry = 1.0f;
    s.foldAmount = 100.0f;
    s.shape = 0.0f;
    s.characterAlgorithm = BassCharacterStage::Algorithm::Tube;
    s.characterAmount = 0.0f;
    s.characterTone = 0.5f;
    s.dropAmountSt = 0.0f;
    s.ampEnv.attackMs = 1.0f;
    s.ampEnv.decayMs = 50.0f;
    s.ampEnv.sustain = 1.0f;
    s.ampEnv.releaseMs = 50.0f;
    s.field.mode = BassStereoField::ImageMode::Solid;
    s.field.spread = 0.0f;
    s.motion.source = (t == VeTarget::Depth || t == VeTarget::Body) ? VeSource::Cycle : VeSource::Drift;
    s.motion.target = t;
    s.motion.amount = 1.0f;
    s.motion.depth = 1.0f;
    s.motion.cycleRateHz = 0.5f;
    s.motion.cycleSync = false;
    s.mass = MassLaw::kNeutralMass;
    s.anchor.waveform = AnchorOscillator::Waveform::Sine;
    s.anchor.mode = AnchorOscillator::Mode::Lock;
    s.anchor.level = 1.0f;
    s.anchorFoldFeed = 0.0f;
    REQUIRE(Krate::DSP::isValidMorphState(s));
    return s;
}

/// Activity endpoints: a random valid pair (plan S5.1 generator, Phase 8) with Motion forced on.
[[nodiscard]] std::pair<MorphState, MorphState> veActivityPair(std::mt19937& rng) {
    std::pair<MorphState, MorphState> ab = BMT::randomValidPair(rng, 0.1f);
    for (MorphState* s : {&ab.first, &ab.second}) {
        if (s->motion.source == VeSource::Off)
            s->motion.source = static_cast<VeSource>(1u + veWord(rng) % 3u);   // Env, Cycle or Drift
        if (!(std::abs(s->motion.amount) > 0.0f))
            s->motion.amount = 0.5f;
        REQUIRE(Krate::DSP::isValidMorphState(*s));
    }
    return ab;
}

[[nodiscard]] const char* veTargetName(VeTarget t) {
    switch (t) {
        case VeTarget::Mass: return "Mass";
        case VeTarget::Depth: return "Depth";
        case VeTarget::Body: return "Body";
        case VeTarget::Edge: return "Edge";
        case VeTarget::Shift: return "Shift";
    }
    return "?";
}

// ---- plan ----

struct VeSoakPlan {
    const char* name = "";
    ProfundumEngine::VoiceMode mode = ProfundumEngine::VoiceMode::Mono;
    std::uint32_t seed = 0;
    std::vector<double> activitySec;          ///< before each lock segment, then the tail (targets.size() + 1)
    double gapSec = 3.0;                      ///< notes off, lock endpoints and neutral settings before s0
    double lockSec = 125.0;
    std::vector<VeTarget> targets;
    std::vector<double> windowOffsetsSec;     ///< t0 of each phase window, relative to s0
    std::vector<std::uint8_t> lockNotes;
};

struct VeSegment {
    std::uint64_t s0 = 0;
    VeTarget target = VeTarget::Depth;
};

struct VeSchedule {
    std::vector<PT::VoiceEngineEvent> events;
    std::vector<MorphState> states;           ///< Endpoint events: index = 2 * stateIndex + (0 A / 1 B)
    std::vector<VeSegment> segments;
    std::uint64_t total = 0;
};

struct VeDefaults {
    NoteMemory::Mode memory = NoteMemory::Mode::AlwaysRetrigger;
    NoteMemory::Retrigger retrigger = NoteMemory::Retrigger::Reset;
    PortaMode portaMode = PortaMode::Always;
    GlideMode glideMode = GlideMode::ConstantTime;
    MonoMode priority = MonoMode::LastNote;
};

void vePush(VeSchedule& s, std::uint64_t at, VeKind k, std::uint8_t note = 0, float value = 0.0f, int index = 0,
            std::uint8_t velocity = 100) {
    s.events.push_back(PT::VoiceEngineEvent{
        .sample = at, .kind = k, .note = note, .velocity = velocity, .value = value, .index = index});
}

void vePushEndpoints(VeSchedule& s, std::uint64_t at, const MorphState& a, const MorphState& b) {
    s.states.push_back(a);
    vePush(s, at, VeKind::Endpoint, 0, 0.0f, static_cast<int>(2 * (s.states.size() - 1)));
    s.states.push_back(b);
    vePush(s, at, VeKind::Endpoint, 0, 0.0f, static_cast<int>(2 * (s.states.size() - 1) + 1));
}

/// Seeded note and automation activity on [a, b) (Motion on, the morph moving); every held note released at b.
void veActivity(VeSchedule& s, std::mt19937& rng, std::uint64_t a, std::uint64_t b, bool poly) {
    if (b <= a)
        return;
    std::vector<std::uint8_t> held;
    const auto release = [&](std::uint64_t at, std::size_t i) {
        vePush(s, at, VeKind::NoteOff, held[i]);
        held.erase(held.begin() + static_cast<std::ptrdiff_t>(i));
    };
    const auto newPair = [&](std::uint64_t at) {
        const std::pair<MorphState, MorphState> ab = veActivityPair(rng);
        vePushEndpoints(s, at, ab.first, ab.second);
    };
    newPair(a);
    vePush(s, a, VeKind::MorphRampMs, 0, static_cast<float>(5.0 + 1995.0 * veUnit(rng)));
    vePush(s, a, VeKind::MorphPosition, 0, static_cast<float>(veUnit(rng)));

    std::uint64_t t = a;
    while (true) {
        t += veSamples(0.02 + 0.6 * veUnit(rng));
        if (t >= b)
            break;
        const std::uint32_t pick = veWord(rng) % 100u;
        const std::size_t maxHeld = poly ? 6 : 3;
        if (pick < 35 || pick >= 90) {
            if (held.size() >= maxHeld) {
                release(t, veWord(rng) % held.size());
                continue;
            }
            const auto note = static_cast<std::uint8_t>(16u + veWord(rng) % 37u);   // E0 ... E3
            if (std::find(held.begin(), held.end(), note) != held.end())
                continue;
            held.push_back(note);
            vePush(s, t, VeKind::NoteOn, note, 0.0f, 0, static_cast<std::uint8_t>(1u + veWord(rng) % 127u));
        } else if (pick < 55) {
            if (!held.empty())
                release(t, veWord(rng) % held.size());
        } else if (pick < 63) {
            vePush(s, t, VeKind::MorphRampMs, 0, static_cast<float>(5.0 + 1995.0 * veUnit(rng)));
            vePush(s, t, VeKind::MorphPosition, 0, static_cast<float>(veUnit(rng)));
        } else if (pick < 71) {
            vePush(s, t, VeKind::Macro, 0, static_cast<float>(veUnit(rng)), static_cast<int>(veWord(rng) % 4u));
        } else if (pick < 76) {
            vePush(s, t, VeKind::PitchBend, 0, static_cast<float>(2.0 * veUnit(rng) - 1.0));
        } else if (pick < 79) {
            vePush(s, t, VeKind::PortamentoTimeMs, 0, static_cast<float>(500.0 * veUnit(rng)));
            vePush(s, t, VeKind::Legato, 0, 0.0f, static_cast<int>(veWord(rng) & 1u));
            vePush(s, t, VeKind::PortamentoMode, 0, 0.0f, static_cast<int>(veWord(rng) % 2u));
            vePush(s, t, VeKind::GlideMode, 0, 0.0f, static_cast<int>(veWord(rng) % 3u));
        } else if (pick < 82) {
            vePush(s, t, VeKind::NoteMemoryMode, 0, 0.0f, static_cast<int>(veWord(rng) % 4u));
            vePush(s, t, VeKind::NoteRetrigger, 0, 0.0f, static_cast<int>(veWord(rng) % 2u));
        } else if (pick < 84) {
            if (poly)
                vePush(s, t, VeKind::Polyphony, 0, 0.0f, static_cast<int>(1u + veWord(rng) % 8u));
            else
                vePush(s, t, VeKind::MonoPriority, 0, 0.0f, static_cast<int>(veWord(rng) % 3u));
        } else if (pick < 86) {
            vePush(s, t, VeKind::Tempo, 0, static_cast<float>(60.0 + 140.0 * veUnit(rng)));
        } else if (pick < 88) {
            vePush(s, t, VeKind::AnchorMorphMode, 0, 0.0f, static_cast<int>(veWord(rng) % 3u));
        } else {
            newPair(t);
        }
    }
    while (!held.empty())
        release(b, held.size() - 1);
}

/// The neutral, glide-free, Drop-free settings of a lock segment, issued at the start of its gap.
void veLockSetup(VeSchedule& s, std::uint64_t at, const MorphState& lock, const VeDefaults& d, bool poly) {
    vePushEndpoints(s, at, lock, lock);
    vePush(s, at, VeKind::MorphRampMs, 0, BassMorphEngine::kMinPositionRampMs);
    vePush(s, at, VeKind::MorphPosition, 0, 0.0f);
    vePush(s, at, VeKind::AnchorMorphMode, 0, 0.0f, static_cast<int>(BassMorphEngine::AnchorMorphMode::Lock));
    for (int m = 0; m < static_cast<int>(ProfundumMacro::Count); ++m)
        vePush(s, at, VeKind::Macro, 0, ProfundumMacroMatrix::kNeutral, m);
    vePush(s, at, VeKind::PitchBend, 0, 0.0f);
    vePush(s, at, VeKind::PortamentoTimeMs, 0, 0.0f);
    vePush(s, at, VeKind::Legato, 0, 0.0f, 0);
    vePush(s, at, VeKind::PortamentoMode, 0, 0.0f, static_cast<int>(d.portaMode));
    vePush(s, at, VeKind::GlideMode, 0, 0.0f, static_cast<int>(d.glideMode));
    vePush(s, at, VeKind::NoteMemoryMode, 0, 0.0f, static_cast<int>(d.memory));
    vePush(s, at, VeKind::NoteRetrigger, 0, 0.0f, static_cast<int>(d.retrigger));
    vePush(s, at, VeKind::MonoPriority, 0, 0.0f, static_cast<int>(d.priority));
    if (poly)
        vePush(s, at, VeKind::Polyphony, 0, 0.0f, static_cast<int>(ProfundumEngine::kDefaultPolyphony));
    vePush(s, at, VeKind::Tempo, 0, ProfundumEngine::kDefaultTempoBpm);
}

[[nodiscard]] VeSchedule veBuildSchedule(const VeSoakPlan& p, const VeDefaults& d) {
    REQUIRE(p.activitySec.size() == p.targets.size() + 1);
    const bool poly = p.mode == ProfundumEngine::VoiceMode::Poly;
    VeSchedule s;
    std::mt19937 rng(p.seed);
    std::uint64_t t = 0;
    for (std::size_t k = 0; k <= p.targets.size(); ++k) {
        const std::uint64_t actEnd = t + veSamples(p.activitySec[k]);
        veActivity(s, rng, t, actEnd, poly);
        t = actEnd;
        if (k == p.targets.size())
            break;
        veLockSetup(s, t, veLockState(p.targets[k]), d, poly);
        const std::uint64_t s0 = t + veSamples(p.gapSec);
        for (const std::uint8_t n : p.lockNotes)
            vePush(s, s0, VeKind::NoteOn, n);
        const std::uint64_t s1 = s0 + veSamples(p.lockSec);
        for (const std::uint8_t n : p.lockNotes)
            vePush(s, s1, VeKind::NoteOff, n);
        s.segments.push_back(VeSegment{.s0 = s0, .target = p.targets[k]});
        t = s1;
    }
    s.total = t;
    std::stable_sort(s.events.begin(), s.events.end(),
                     [](const PT::VoiceEngineEvent& x, const PT::VoiceEngineEvent& y) { return x.sample < y.sample; });
    return s;
}

// ---- observer (R-K: in-place finiteness, phase windows only) ----

constexpr std::size_t kVeVoiceTapCount = 13;
constexpr std::array<const char*, kVeVoiceTapCount> kVeVoiceTapNames{
    "coreL", "coreR", "charL", "charR", "anchorL", "anchorR", "gAmp (amp envelope)", "gD (RetriggerDeclick)", "fRes",
    "fCore (DropEnvelope / DropPhaseLock)", "fAnchor", "anchorPhase", "feed"};
constexpr std::size_t kVeEngineTapCount = 10;
constexpr std::array<const char*, kVeEngineTapCount> kVeEngineTapNames{
    "sumL", "sumR", "dynL", "dynR", "fieldL (BassStereoField)", "fieldR (BassStereoField)", "dcL (DCBlocker)",
    "dcR (DCBlocker)", "outL (TruePeakLimiter)", "outR (TruePeakLimiter)"};
constexpr std::size_t kVeGridCount = 6;
constexpr std::array<const char*, kVeGridCount> kVeGridNames{
    "resolved (BassMorphEngine)", "afterMacros", "afterMotion", "motionSource / motionOutput (LFO / Drift / ENV)",
    "mass / anchor / compression scalars (MassLaw smoothedMass)", "targets (MassLaw handed)"};
constexpr std::size_t kVeChannels = 4;   // anchor, core, character, f_core

class VeSoakObserver final : public ProfundumEngineObserver {
public:
    explicit VeSoakObserver(std::size_t capLen)
        : cap_(capLen), buf_(ProfundumEngine::kMaxVoices * kVeMaxWindows * kVeChannels * capLen, 0.0f) {}

    std::array<std::uint64_t, kVeVoiceTapCount> voiceBad{};
    std::uint64_t voiceScalarBad = 0;
    std::array<std::uint64_t, kVeEngineTapCount> engineBad{};
    std::array<std::uint64_t, kVeGridCount> gridBad{};
    std::uint64_t voiceChunks = 0, engineChunks = 0, gridRecords = 0;
    std::uint64_t firstBadSample = std::numeric_limits<std::uint64_t>::max();

    // SC-013 DC at the DC-safety tap (G11-4, user ruling 2026-10-20): the same BH7-weighted mean per 10 s window as
    // the output's, read from the engine's dcL / dcR tap (FR-025's DCBlocker, before the TruePeakLimiter). This is
    // the gated reading; the output's windows are recorded (the limiter's gain dips on a hot asymmetric onset leave
    // a DC lobe that is gain modulation, not build-up; the trend clause gates build-up at the output).
    std::vector<double> dcTapDb;
    std::vector<std::size_t> dcTapCount;   ///< samples accumulated per window (== the window length when every sample was chunked)
    // G14-3 POLY (2026-10-21): per voice, |f_core / f_res - 1| in cents (the Drop / lock offset the Anchor does not
    // follow in LOCK: f_anchor = f_res), its maximum and the first / last sample above 1 cent. Record.
    std::array<double, ProfundumEngine::kMaxVoices> detuneMaxCents{};
    std::array<std::uint64_t, ProfundumEngine::kMaxVoices> detuneFirst{}, detuneLast{};
    std::array<std::uint64_t, ProfundumEngine::kMaxVoices> detuneMaxAt{};
    std::array<double, ProfundumEngine::kMaxVoices> segDetuneCents{};     ///< while armed, from the first window's start (reset by arm())
    std::array<std::uint64_t, ProfundumEngine::kMaxVoices> segDetuneAt{};
    std::array<std::uint64_t, ProfundumEngine::kMaxVoices> segDetuneCount{};   ///< samples > 1 cent in that span
    std::array<std::uint64_t, ProfundumEngine::kMaxVoices> segDetuneFirst{};   ///< first sample > 1 cent in that span (0 = none)
    std::array<float, ProfundumEngine::kMaxVoices> segFResAtStart{}, segFCoreAtStart{};   ///< at the first window's start
    std::string segGridTrace;   ///< round 10: the first grid records after arming (resolved Drop fields, position, Motion)
    void setDcWindow(const std::vector<double>* bh, double bhSum, std::size_t len) noexcept {
        dcBh_ = bh;
        dcBhSum_ = bhSum;
        dcLen_ = len;
    }

    void arm(const std::vector<std::uint64_t>& starts) {
        REQUIRE(starts.size() <= kVeMaxWindows);
        nWin_ = starts.size();
        for (std::size_t w = 0; w < nWin_; ++w)
            start_[w] = starts[w];
        filled_.fill(0);
        segDetuneCents.fill(0.0);
        segDetuneAt.fill(0);
        segDetuneCount.fill(0);
        segDetuneFirst.fill(0);
        segFResAtStart.fill(0.0f);
        segFCoreAtStart.fill(0.0f);
        segGridTrace.clear();
        gridTraceLeft_ = 6;
        armed_ = true;
    }
    void disarm() noexcept { armed_ = false; }

    [[nodiscard]] std::size_t filled(std::size_t v, std::size_t w) const noexcept { return filled_[v * kVeMaxWindows + w]; }
    [[nodiscard]] const float* window(std::size_t v, std::size_t w, std::size_t ch) const noexcept {
        return buf_.data() + ((v * kVeMaxWindows + w) * kVeChannels + ch) * cap_;
    }

    void onGrid(const ProfundumGridRecord& g) noexcept override {
        ++gridRecords;
        if (armed_ && gridTraceLeft_ > 0 && g.resolved != nullptr) {
            --gridTraceLeft_;
            const MorphState& s = g.resolved->state;
            std::ostringstream t;
            t << std::setprecision(5) << "[grid " << g.sample << " p " << g.resolved->position << " drop " << s.dropAmountSt
              << " st / " << s.dropTimeMs << " ms shape " << s.dropShape << " rebound " << s.dropRebound;
            if (g.afterMotion != nullptr)
                t << " afterMotion drop " << g.afterMotion->dropAmountSt;
            t << " motion " << g.motionOutput << " mass " << g.massIn << "] ";
            segGridTrace += t.str();
        }
        const auto bad = [&](std::size_t k) noexcept {
            ++gridBad[k];
            firstBadSample = std::min(firstBadSample, g.sample);
        };
        if (g.resolved != nullptr) {
            const BassMorphEngine::Resolved& r = *g.resolved;
            if (!veStateFinite(r.state) || !Krate::DSP::detail::isFinite(r.position)
                || !Krate::DSP::detail::isFinite(r.anchorPosition) || !Krate::DSP::detail::isFinite(r.anchorMass))
                bad(0);
        }
        if (g.afterMacros != nullptr && !veStateFinite(*g.afterMacros))
            bad(1);
        if (g.afterMotion != nullptr && !veStateFinite(*g.afterMotion))
            bad(2);
        if (!Krate::DSP::detail::isFinite(g.motionSource) || !Krate::DSP::detail::isFinite(g.motionOutput))
            bad(3);
        bool scalars = true;
        for (const float x : {g.massIn, g.smoothedMass, g.anchorMassUsed, g.anchorLevel, g.compressionHanded,
                              g.compressorGrDb})
            scalars = scalars && Krate::DSP::detail::isFinite(x);
        if (!scalars)
            bad(4);
        bool targets = true;
        for (const float x : {g.targets.depth, g.targets.body, g.targets.transientDurationMs, g.targets.foldAmount,
                              g.targets.compressionAmount, g.targets.anchorLevel, g.targets.outputTrimDb})
            targets = targets && Krate::DSP::detail::isFinite(x);
        if (!targets)
            bad(5);
    }

    void onVoiceChunk(const ProfundumVoiceTaps& v) noexcept override {
        ++voiceChunks;
        if (v.voice < ProfundumEngine::kMaxVoices && v.fRes.size() >= v.n && v.fCore.size() >= v.n) {
            for (std::size_t i = 0; i < v.n; ++i) {
                const float fr = v.fRes[i];
                const float fc = v.fCore[i];
                if (!(fr > 0.0f) || !(fc > 0.0f))
                    continue;
                const double cents = std::abs(1200.0 * std::log2(static_cast<double>(fc) / static_cast<double>(fr)));
                if (cents > detuneMaxCents[v.voice]) {
                    detuneMaxCents[v.voice] = cents;
                    detuneMaxAt[v.voice] = v.sample + i;
                }
                if (armed_ && nWin_ > 0 && v.sample + i >= start_[0]) {
                    if (cents > segDetuneCents[v.voice]) {
                        segDetuneCents[v.voice] = cents;
                        segDetuneAt[v.voice] = v.sample + i;
                    }
                    if (cents > 1.0) {
                        ++segDetuneCount[v.voice];
                        if (segDetuneFirst[v.voice] == 0)
                            segDetuneFirst[v.voice] = v.sample + i;
                    }
                    if (v.sample + i == start_[0]) {
                        segFResAtStart[v.voice] = fr;
                        segFCoreAtStart[v.voice] = fc;
                    }
                }
                if (cents > 1.0) {
                    if (detuneFirst[v.voice] == 0)
                        detuneFirst[v.voice] = v.sample + i;
                    detuneLast[v.voice] = v.sample + i;
                }
            }
        }
        const std::array<std::span<const float>, kVeVoiceTapCount> taps{
            v.coreL, v.coreR, v.charL, v.charR, v.anchorL, v.anchorR, v.gAmp,
            v.gD,    v.fRes,  v.fCore, v.fAnchor, v.anchorPhase, v.feed};
        for (std::size_t k = 0; k < kVeVoiceTapCount; ++k) {
            const std::uint64_t b = veCountNonFinite(taps[k], v.n);
            if (b != 0) {
                voiceBad[k] += b;
                firstBadSample = std::min(firstBadSample, v.sample);
            }
        }
        bool scalars = true;
        for (const float x : {v.followRatio, v.a1, v.anchorLevel, v.feedFrom, v.feedTo, v.meterTheta, v.meterFreqHz})
            scalars = scalars && Krate::DSP::detail::isFinite(x);
        if (!scalars) {
            ++voiceScalarBad;
            firstBadSample = std::min(firstBadSample, v.sample);
        }
        if (!armed_ || v.voice >= ProfundumEngine::kMaxVoices)
            return;
        const std::array<std::span<const float>, kVeChannels> src{v.anchorL, v.coreL, v.charL, v.fCore};
        const std::uint64_t c0 = v.sample;
        const std::uint64_t c1 = v.sample + std::min<std::uint64_t>(v.n, v.coreL.size());
        for (std::size_t w = 0; w < nWin_; ++w) {
            const std::uint64_t w0 = start_[w];
            const std::uint64_t w1 = w0 + cap_;
            const std::uint64_t a = std::max(c0, w0);
            const std::uint64_t b = std::min(c1, w1);
            if (a >= b)
                continue;
            for (std::size_t ch = 0; ch < kVeChannels; ++ch) {
                if (src[ch].size() < static_cast<std::size_t>(b - c0))
                    continue;
                float* dst = buf_.data() + ((static_cast<std::size_t>(v.voice) * kVeMaxWindows + w) * kVeChannels + ch) * cap_;
                for (std::uint64_t s = a; s < b; ++s)
                    dst[s - w0] = src[ch][static_cast<std::size_t>(s - c0)];
            }
            filled_[static_cast<std::size_t>(v.voice) * kVeMaxWindows + w] += static_cast<std::size_t>(b - a);
        }
    }

    void onEngineChunk(const ProfundumEngineTaps& e) noexcept override {
        ++engineChunks;
        const std::array<std::span<const float>, kVeEngineTapCount> taps{e.sumL,   e.sumR, e.dynL, e.dynR, e.fieldL,
                                                                         e.fieldR, e.dcL,  e.dcR,  e.outL, e.outR};
        for (std::size_t k = 0; k < kVeEngineTapCount; ++k) {
            const std::uint64_t b = veCountNonFinite(taps[k], e.n);
            if (b != 0) {
                engineBad[k] += b;
                firstBadSample = std::min(firstBadSample, e.sample);
            }
        }
        if (dcBh_ == nullptr || dcLen_ == 0 || e.dcL.size() < e.n || e.dcR.size() < e.n)
            return;
        for (std::size_t i = 0; i < e.n; ++i) {
            const std::uint64_t s = e.sample + i;
            const std::uint64_t w = s / dcLen_;
            if (w != dcWin_) {   // a window left without its last sample: close it on what it holds
                if (dcCount_ > 0)
                    pushDcWindow();
                dcWin_ = w;
            }
            const float l = e.dcL[i];
            const float r = e.dcR[i];
            if (!Krate::DSP::detail::isFinite(l) || !Krate::DSP::detail::isFinite(r))
                continue;
            const auto k = static_cast<std::size_t>(s % dcLen_);
            dcAccL_ += (*dcBh_)[k] * static_cast<double>(l);
            dcAccR_ += (*dcBh_)[k] * static_cast<double>(r);
            ++dcCount_;
            if (k + 1 == dcLen_)
                pushDcWindow();
        }
    }

private:
    void pushDcWindow() {
        dcTapDb.push_back(std::max(veDb(dcAccL_ / dcBhSum_), veDb(dcAccR_ / dcBhSum_)));
        dcTapCount.push_back(dcCount_);
        dcAccL_ = 0.0;
        dcAccR_ = 0.0;
        dcCount_ = 0;
    }

    const std::vector<double>* dcBh_ = nullptr;
    double dcBhSum_ = 1.0;
    std::size_t dcLen_ = 0;
    double dcAccL_ = 0.0, dcAccR_ = 0.0;
    std::size_t dcCount_ = 0;
    std::uint64_t dcWin_ = 0;
    int gridTraceLeft_ = 0;
    std::size_t cap_;
    std::vector<float> buf_;
    std::array<std::size_t, ProfundumEngine::kMaxVoices * kVeMaxWindows> filled_{};
    std::array<std::uint64_t, kVeMaxWindows> start_{};
    std::size_t nWin_ = 0;
    bool armed_ = false;
};

// ---- SC-014 analysis ----

struct VeLockReading {
    std::size_t segment = 0;
    VeTarget target = VeTarget::Depth;
    std::size_t voice = 0;
    int note = 0;
    double hz = 0.0;
    std::vector<double> dCore;    ///< Delta-theta (Anchor - core-tap h1) per window, degrees
    std::vector<double> dChar;    ///< Delta-theta (Anchor - Character-tap h1) per window (H-2, recorded)
    std::vector<double> sCore;    ///< |Delta-theta(t0 + span) - Delta-theta(t0)|, gated
    std::vector<double> sChar;    ///< the same through Shape -> Fold -> Character (recorded)
};

struct VeSegmentAudit {
    std::size_t segment = 0;
    std::vector<int> unmatchedNotes;
    std::vector<std::string> notes;
};

void veAnalyseSegment(const VeSoakObserver& obs, const VeSoakPlan& p, std::size_t seg, VeTarget target,
                      std::size_t capLen, std::vector<VeLockReading>& out, VeSegmentAudit& audit) {
    audit.segment = seg;
    const std::size_t nWin = p.windowOffsetsSec.size();
    std::vector<int> matches(p.lockNotes.size(), 0);
    for (std::size_t v = 0; v < ProfundumEngine::kMaxVoices; ++v) {
        bool complete = true;
        for (std::size_t w = 0; w < nWin; ++w)
            complete = complete && obs.filled(v, w) == capLen;
        if (!complete)
            continue;
        double fSum = 0.0;
        double eCore = 0.0;
        for (std::size_t w = 0; w < nWin; ++w) {
            const float* f = obs.window(v, w, 3);
            const float* c = obs.window(v, w, 1);
            for (std::size_t i = 0; i < capLen; ++i) {
                fSum += static_cast<double>(f[i]);
                eCore += static_cast<double>(c[i]) * static_cast<double>(c[i]);
            }
        }
        const double hz = fSum / static_cast<double>(capLen * nWin);
        const double rms = std::sqrt(eCore / static_cast<double>(capLen * nWin));
        std::size_t match = p.lockNotes.size();
        for (std::size_t k = 0; k < p.lockNotes.size(); ++k) {
            const double fn = static_cast<double>(PT::anchorMidiHz(p.lockNotes[k]));
            if (std::abs(hz / fn - 1.0) < 0.03)
                match = k;
        }
        if (match == p.lockNotes.size() || !(rms > 1.0e-6)) {
            std::ostringstream os;
            os << std::setprecision(6) << "voice " << v << " rendered all windows but matched no held note (mean f_core "
               << hz << " Hz, core rms " << rms << "): not windowed";
            audit.notes.push_back(os.str());
            continue;
        }
        ++matches[match];
        const std::size_t L = veLockWindowLength(kVeFs, hz);
        REQUIRE(L <= capLen);
        VeLockReading r;
        r.segment = seg;
        r.target = target;
        r.voice = v;
        r.note = p.lockNotes[match];
        r.hz = hz;
        for (std::size_t w = 0; w < nWin; ++w) {
            const double ta = PT::goertzelPhase(obs.window(v, w, 0), L, kVeFs, hz);
            const double tc = PT::goertzelPhase(obs.window(v, w, 1), L, kVeFs, hz);
            const double th = PT::goertzelPhase(obs.window(v, w, 2), L, kVeFs, hz);
            r.dCore.push_back(veWrapDeg((ta - tc) * kVeRadToDeg));
            r.dChar.push_back(veWrapDeg((ta - th) * kVeRadToDeg));
        }
        for (std::size_t w = 0; w + 1 < nWin; ++w) {
            r.sCore.push_back(std::abs(veWrapDeg(r.dCore[w + 1] - r.dCore[w])));
            r.sChar.push_back(std::abs(veWrapDeg(r.dChar[w + 1] - r.dChar[w])));
        }
        out.push_back(std::move(r));
    }
    for (std::size_t k = 0; k < p.lockNotes.size(); ++k)
        if (matches[k] != 1)
            audit.unmatchedNotes.push_back(p.lockNotes[k]);
}

// ---- the soak ----

struct VeSoakResult {
    std::uint64_t total = 0;
    std::uint64_t outNonFinite = 0;
    std::uint64_t firstOutNonFinite = std::numeric_limits<std::uint64_t>::max();
    double truePeakDb = -300.0;
    std::uint64_t truePeakSample = 0;
    double ceilingDb = 0.0;
    std::vector<double> dcDb;                 ///< per 10 s window, worst channel, at the output (recorded, G11-4)
    std::vector<bool> dcWindowHasNote;        ///< G14-2: the window holds a note-on / note-off (recorded, not gated)
    std::vector<std::array<double, ProfundumEngine::kMaxVoices>> segDetuneCents;   ///< G14-3: per segment, per voice max |f_core / f_res - 1| while armed
    std::vector<std::array<std::uint64_t, ProfundumEngine::kMaxVoices>> segDetuneAt;
    std::vector<std::array<std::uint64_t, ProfundumEngine::kMaxVoices>> segDetuneCount;
    std::vector<std::array<std::uint64_t, ProfundumEngine::kMaxVoices>> segDetuneFirst;
    std::vector<std::array<float, ProfundumEngine::kMaxVoices>> segFResAtStart, segFCoreAtStart;
    std::vector<std::string> segGridTrace;
    std::vector<double> dcTapDb;              ///< per 10 s window, worst channel, at the DC-safety tap (gated, G11-4)
    std::vector<std::size_t> dcTapCount;      ///< samples the DC-tap window accumulated
    std::array<double, ProfundumEngine::kMaxVoices> detuneMaxCents{};   ///< G14-3 record
    std::array<std::uint64_t, ProfundumEngine::kMaxVoices> detuneFirst{}, detuneLast{}, detuneMaxAt{};
    std::size_t trendWindows = 0;
    double trendFirstDb = 0.0, trendLastDb = 0.0;          ///< over the windows without note events (G14-2)
    std::size_t trendAllWindows = 0;
    double trendAllFirstDb = 0.0, trendAllLastDb = 0.0;    ///< over every window (recorded)
    VeStatePoll poll;
    std::array<std::uint64_t, kVeVoiceTapCount> voiceBad{};
    std::uint64_t voiceScalarBad = 0;
    std::array<std::uint64_t, kVeEngineTapCount> engineBad{};
    std::array<std::uint64_t, kVeGridCount> gridBad{};
    std::uint64_t voiceChunks = 0, engineChunks = 0, gridRecords = 0;
    std::uint64_t firstTapBad = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t endpointRejected = 0;
    std::uint64_t droppedEvents = 0;
    std::size_t events = 0;
    std::vector<VeLockReading> lock;
    std::vector<VeSegmentAudit> audits;
    std::size_t segmentsAnalysed = 0;
    double wallSec = 0.0;
};

[[nodiscard]] VeSoakResult veRunSoak(const VeSoakPlan& p) {
    REQUIRE(p.windowOffsetsSec.size() >= 2);
    REQUIRE(p.windowOffsetsSec.size() <= kVeMaxWindows);
    REQUIRE(p.windowOffsetsSec.back() + kVeCaptureSec <= p.lockSec);
    const auto wallStart = std::chrono::steady_clock::now();

    auto e = std::make_unique<ProfundumEngine>();
    const VeDefaults defaults{.memory = e->noteMemoryMode(),
                              .retrigger = e->noteRetrigger(),
                              .portaMode = e->portamentoMode(),
                              .glideMode = e->glideMode(),
                              .priority = e->monoPriority()};
    const VeSchedule sched = veBuildSchedule(p, defaults);
    const std::size_t capLen = static_cast<std::size_t>(veSamples(kVeCaptureSec));

    VeSoakResult res;
    res.total = sched.total;
    res.events = sched.events.size();
    VeSoakObserver obs(capLen);

    const MorphState first = veLockState(VeTarget::Depth);
    e->setSeed(p.seed);
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::A, first));
    REQUIRE(e->setEndpoint(BassMorphEngine::Endpoint::B, first));
    e->setVoiceMode(p.mode);
    e->setPolyphony(ProfundumEngine::kDefaultPolyphony);
    e->setObserver(&obs);
    e->prepare(kVeFs);
    res.ceilingDb = static_cast<double>(e->outputCeilingDb());

    // SC-013 DC: the BH7-weighted mean of each 10 s window (bin 0 of a 7-term Blackman-Harris DFT); the window's
    // side lobes keep the sub-bass content (>= 20 Hz, 200 bins away) out of the DC reading.
    const std::size_t dcLen = static_cast<std::size_t>(veSamples(kVeDcWindowSec));
    const std::vector<double> bh = PT::bh7Window(dcLen);
    double bhSum = 0.0;
    for (const double w : bh)
        bhSum += w;
    double accL = 0.0, accR = 0.0;
    obs.setDcWindow(&bh, bhSum, dcLen);   // G11-4: the gated reading, at the DC-safety tap
    // G14-2 (user ruling 2026-10-21): a window that holds a note-on / note-off is recorded, not gated: a DC step
    // Delta at the 5 Hz blocker's input leaves a transient whose 10 s mean is Delta * tau / 10 s (-64 dBFS for
    // Delta 0.21), so the -80 dBFS bar is read on the windows without note events (the lock segments).
    std::vector<std::uint64_t> noteEventSamples;
    for (const PT::VoiceEngineEvent& ev : sched.events)
        if (ev.kind == VeKind::NoteOn || ev.kind == VeKind::NoteOff)
            noteEventSamples.push_back(ev.sample);
    std::size_t noteEventIdx = 0;

    // SC-013 true peak: test-side 4x polyphase halfband per channel.
    PolyphaseHalfbandOversampler hbL, hbR;
    hbL.setFactor(4);
    hbR.setFactor(4);
    float peak = 0.0f;
    std::array<float, PolyphaseHalfbandOversampler::kMaxFactor> up{};

    PT::VoiceEngineScript dummy;   // voiceEngineIssue's script is read only for Endpoint, which is handled here
    std::array<float, kVeHostBlock> outL{}, outR{};
    std::size_t ei = 0;
    std::uint64_t pos = 0;
    std::size_t seg = 0;
    bool armed = false;
    const std::uint64_t lastOffset = veSamples(p.windowOffsetsSec.back());
    while (pos < sched.total) {
        while (ei < sched.events.size() && sched.events[ei].sample <= pos) {
            const PT::VoiceEngineEvent& ev = sched.events[ei++];
            if (ev.kind == VeKind::Endpoint) {
                const auto which = (ev.index % 2 == 0) ? BassMorphEngine::Endpoint::A : BassMorphEngine::Endpoint::B;
                if (!e->setEndpoint(which, sched.states[static_cast<std::size_t>(ev.index) / 2]))
                    ++res.endpointRejected;
            } else {
                PT::voiceEngineIssue(*e, ev, dummy);
            }
        }
        std::uint64_t next = std::min<std::uint64_t>(pos + kVeHostBlock, sched.total);
        if (ei < sched.events.size() && sched.events[ei].sample < next)
            next = sched.events[ei].sample;
        const auto n = static_cast<std::size_t>(next - pos);
        e->processBlock(outL.data(), outR.data(), n);
        vePollEngineState(*e, res.poll);
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint64_t s = pos + i;
            const float l = outL[i];
            const float r = outR[i];
            if (!Krate::DSP::detail::isFinite(l) || !Krate::DSP::detail::isFinite(r)) {
                ++res.outNonFinite;
                res.firstOutNonFinite = std::min(res.firstOutNonFinite, s);
                continue;
            }
            hbL.upsample(l, up.data());
            for (const float u : up)
                if (std::abs(u) > peak) {
                    peak = std::abs(u);
                    res.truePeakSample = s;
                }
            hbR.upsample(r, up.data());
            for (const float u : up)
                if (std::abs(u) > peak) {
                    peak = std::abs(u);
                    res.truePeakSample = s;
                }
            const auto k = static_cast<std::size_t>(s % dcLen);
            accL += bh[k] * static_cast<double>(l);
            accR += bh[k] * static_cast<double>(r);
            if (k + 1 == dcLen) {
                res.dcDb.push_back(std::max(veDb(accL / bhSum), veDb(accR / bhSum)));
                const std::uint64_t w0 = s + 1 - dcLen;
                bool hasNote = false;
                while (noteEventIdx < noteEventSamples.size() && noteEventSamples[noteEventIdx] <= s) {
                    if (noteEventSamples[noteEventIdx] >= w0)
                        hasNote = true;
                    ++noteEventIdx;
                }
                res.dcWindowHasNote.push_back(hasNote);
                accL = 0.0;
                accR = 0.0;
            }
        }
        pos = next;

        if (seg < sched.segments.size()) {
            const std::uint64_t s0 = sched.segments[seg].s0;
            if (!armed && pos >= s0) {
                std::vector<std::uint64_t> starts;
                for (const double o : p.windowOffsetsSec)
                    starts.push_back(s0 + veSamples(o));
                obs.arm(starts);
                armed = true;
            }
            if (armed && pos >= s0 + lastOffset + capLen) {
                res.segDetuneCents.push_back(obs.segDetuneCents);
                res.segDetuneAt.push_back(obs.segDetuneAt);
                res.segDetuneCount.push_back(obs.segDetuneCount);
                res.segDetuneFirst.push_back(obs.segDetuneFirst);
                res.segFResAtStart.push_back(obs.segFResAtStart);
                res.segFCoreAtStart.push_back(obs.segFCoreAtStart);
                res.segGridTrace.push_back(obs.segGridTrace);
                obs.disarm();
                VeSegmentAudit audit;
                veAnalyseSegment(obs, p, seg, sched.segments[seg].target, capLen, res.lock, audit);
                res.audits.push_back(std::move(audit));
                ++res.segmentsAnalysed;
                armed = false;
                ++seg;
            }
        }
    }
    e->setObserver(nullptr);

    res.truePeakDb = veDb(static_cast<double>(peak));
    res.voiceBad = obs.voiceBad;
    res.voiceScalarBad = obs.voiceScalarBad;
    res.engineBad = obs.engineBad;
    res.gridBad = obs.gridBad;
    res.voiceChunks = obs.voiceChunks;
    res.engineChunks = obs.engineChunks;
    res.gridRecords = obs.gridRecords;
    res.firstTapBad = obs.firstBadSample;
    res.dcTapDb = obs.dcTapDb;
    res.dcTapCount = obs.dcTapCount;
    res.detuneMaxCents = obs.detuneMaxCents;
    res.detuneFirst = obs.detuneFirst;
    res.detuneLast = obs.detuneLast;
    res.detuneMaxAt = obs.detuneMaxAt;
    res.droppedEvents = e->droppedEvents();

    // SC-013 trend: the mean |DC| (linear, then dB) of the first and the last 5 min of windows; a run shorter than
    // 10 min compares its first and last halves (the smoke: one 10 s window each).
    // G14-2: the gated trend reads the windows without note events (the windows with a note-on / note-off carry
    // the activity's Delta * tau / T transients, which the first run's +1 dB clause measured as -71.48 vs -72.78
    // between two activity stretches); the all-window trend is recorded.
    const auto trendOf = [&](const std::vector<std::size_t>& idx, std::size_t& nWin, double& first, double& last,
                             const std::vector<double>& series) {
        nWin = std::min(static_cast<std::size_t>(kVeTrendSec / kVeDcWindowSec), idx.size() / 2);
        if (nWin == 0)
            return;
        const auto meanDb = [&](std::size_t from) {
            double m = 0.0;
            for (std::size_t i = from; i < from + nWin; ++i)
                m += std::pow(10.0, (idx[i] < series.size() ? series[idx[i]] : -300.0) / 20.0);
            return veDb(m / static_cast<double>(nWin));
        };
        first = meanDb(0);
        last = meanDb(idx.size() - nWin);
    };
    std::vector<std::size_t> allIdx, quietIdx;
    for (std::size_t i = 0; i < res.dcDb.size(); ++i) {
        allIdx.push_back(i);
        if (i >= res.dcWindowHasNote.size() || !res.dcWindowHasNote[i])
            quietIdx.push_back(i);
    }
    // Round 6: the gated trend reads the DC-safety tap (the engine's own DC; the output's quiet windows carry the
    // limiter's gain-modulation lobe on the hot POLY lock notes, -67 .. -60 dBFS); the output's all-window trend is
    // recorded.
    trendOf(quietIdx, res.trendWindows, res.trendFirstDb, res.trendLastDb, res.dcTapDb);
    trendOf(allIdx, res.trendAllWindows, res.trendAllFirstDb, res.trendAllLastDb, res.dcDb);
    res.wallSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - wallStart).count();
    return res;
}

[[nodiscard]] std::string veReport(const VeSoakPlan& p, const VeSoakResult& r) {
    std::ostringstream os;
    os << std::setprecision(6);
    os << "VoiceEngine soak '" << p.name << "' (SC-013 / SC-014; tasks.md T019)\n";
    os << "mode " << (p.mode == ProfundumEngine::VoiceMode::Mono ? "MONO" : "POLY") << ", seed 0x" << std::hex << p.seed
       << std::dec << ", fs " << kVeFs << ", host block " << kVeHostBlock << ", " << r.total << " samples ("
       << static_cast<double>(r.total) / kVeFs << " s), " << r.events << " events, wall " << r.wallSec << " s\n";
    os << "lock notes:";
    for (const std::uint8_t n : p.lockNotes)
        os << ' ' << static_cast<int>(n);
    os << "; lock " << p.lockSec << " s after a " << p.gapSec << " s gap; windows at s0 +";
    for (const double o : p.windowOffsetsSec)
        os << ' ' << o << " s";
    os << "\n\n[SC-013] finite (bit test)\n";
    os << "  output non-finite samples: " << r.outNonFinite << (r.outNonFinite != 0 ? "  RED" : "  green") << '\n';
    for (std::size_t k = 0; k < kVeVoiceTapCount; ++k)
        os << "  voice tap " << kVeVoiceTapNames[k] << ": " << r.voiceBad[k] << " non-finite\n";
    os << "  voice scalars (followRatio, a1, anchorLevel, feed, meter): " << r.voiceScalarBad << " chunks\n";
    for (std::size_t k = 0; k < kVeEngineTapCount; ++k)
        os << "  engine tap " << kVeEngineTapNames[k] << ": " << r.engineBad[k] << " non-finite\n";
    for (std::size_t k = 0; k < kVeGridCount; ++k)
        os << "  grid " << kVeGridNames[k] << ": " << r.gridBad[k] << " records\n";
    os << "  observed: " << r.voiceChunks << " voice chunks, " << r.engineChunks << " engine chunks, " << r.gridRecords
       << " grid records\n";
    os << "  state poll (engine stateFinite / oversamplerStateFinite), " << r.poll.polls << " polls: "
       << (r.poll.stateAvailable ? "stateFinite polled, " + std::to_string(r.poll.stateBad) + " false"
                                 : std::string("stateFinite UNAVAILABLE (DEFECT, run-log T019)"))
       << "; "
       << (r.poll.oversamplerAvailable ? "oversamplerStateFinite polled, " + std::to_string(r.poll.oversamplerBad) + " false"
                                       : std::string("oversamplerStateFinite UNAVAILABLE (DEFECT, run-log T019)"))
       << '\n';
    os << "\n[SC-013] true peak (test-side 4x halfband): " << r.truePeakDb << " dBTP at sample " << r.truePeakSample
       << "; bar ceiling " << r.ceilingDb << " + " << kVeTruePeakMarginDb << " dB"
       << (r.truePeakDb <= r.ceilingDb + kVeTruePeakMarginDb ? "  green" : "  RED") << '\n';
    double worstDc = -300.0;        // gated: windows without note events
    double worstDcNote = -300.0;    // recorded: windows with a note-on / note-off (G14-2)
    std::size_t gatedWindows = 0, noteWindows = 0;
    for (std::size_t i = 0; i < r.dcTapDb.size(); ++i) {
        const bool hasNote = i < r.dcWindowHasNote.size() && r.dcWindowHasNote[i];
        if (hasNote) {
            worstDcNote = std::max(worstDcNote, r.dcTapDb[i]);
            ++noteWindows;
        } else {
            worstDc = std::max(worstDc, r.dcTapDb[i]);
            ++gatedWindows;
        }
    }
    double worstOut = -300.0;
    for (const double d : r.dcDb)
        worstOut = std::max(worstOut, d);
    os << "\n[SC-013] DC per " << kVeDcWindowSec << " s window (BH7-weighted mean, worst channel) at the DC-safety tap "
       << "(DCBlocker, ruling G11-4), bar " << kVeDcBarDb << " dBFS over the " << gatedWindows
       << " windows without note events (ruling G14-2): worst " << worstDc
       << (worstDc <= kVeDcBarDb ? "  green" : "  RED") << "; the " << noteWindows
       << " windows with a note-on / note-off recorded: worst " << worstDcNote
       << " dBFS (a DC step Delta at the 5 Hz blocker leaves Delta * tau / 10 s); output (TruePeakLimiter) recorded: worst "
       << worstOut << " dBFS\n";
    for (std::size_t i = 0; i < r.dcTapDb.size(); ++i)
        os << "  window " << i << " [" << static_cast<double>(i) * kVeDcWindowSec << " s]: DC tap " << r.dcTapDb[i]
           << " dBFS (" << r.dcTapCount[i] << " samples"
           << ((i < r.dcWindowHasNote.size() && r.dcWindowHasNote[i]) ? ", note events: recorded" : ", gated")
           << "), output " << (i < r.dcDb.size() ? r.dcDb[i] : -300.0) << " dBFS\n";
    os << "  trend over " << r.trendWindows << " windows without note events (DC-safety tap, G14-2 round 6): first " << r.trendFirstDb
       << " dBFS, last " << r.trendLastDb << " dBFS, bar last <= first + " << kVeTrendBarDb << " dB"
       << ((r.trendLastDb <= r.trendFirstDb + kVeTrendBarDb || r.trendLastDb <= kVeTrendFloorDb) ? "  green" : "  RED")
       << " (or last <= " << kVeTrendFloorDb << " dBFS, the build-up floor)" << "; the output's all " << r.trendAllWindows
       << "-window trend recorded: first " << r.trendAllFirstDb << " dBFS, last " << r.trendAllLastDb << " dBFS\n";
    os << "\n[record] f_core vs f_res per voice (G14-3; the Drop / lock offset the Anchor does not follow in LOCK): ";
    for (std::size_t v = 0; v < ProfundumEngine::kMaxVoices; ++v) {
        if (r.detuneMaxCents[v] <= 0.0)
            continue;
        os << "voice " << v << " max " << r.detuneMaxCents[v] << " cents at " << r.detuneMaxAt[v] << " ("
           << static_cast<double>(r.detuneMaxAt[v]) / kVeFs << " s), > 1 cent over [" << r.detuneFirst[v] << ", "
           << r.detuneLast[v] << "] (" << static_cast<double>(r.detuneFirst[v]) / kVeFs << " .. "
           << static_cast<double>(r.detuneLast[v]) / kVeFs << " s); ";
    }
    os << '\n';
    for (std::size_t sIdx = 0; sIdx < r.segGridTrace.size(); ++sIdx)
        os << "[record] segment " << sIdx << " first grid records after s0 (round 10): " << r.segGridTrace[sIdx] << '\n';
    os << "[record] f_core vs f_res from each segment's first window start (G14-3), per segment and voice: ";
    for (std::size_t sIdx = 0; sIdx < r.segDetuneCents.size(); ++sIdx)
        for (std::size_t v = 0; v < ProfundumEngine::kMaxVoices; ++v)
            if (r.segDetuneCents[sIdx][v] > 0.01)
                os << "seg " << sIdx << " voice " << v << " max " << r.segDetuneCents[sIdx][v] << " cents at "
                   << static_cast<double>(r.segDetuneAt[sIdx][v]) / kVeFs << " s (" << r.segDetuneCount[sIdx][v]
                   << " samples > 1 cent, first at " << static_cast<double>(r.segDetuneFirst[sIdx][v]) / kVeFs
                   << " s; at the window start f_res " << r.segFResAtStart[sIdx][v] << " f_core "
                   << r.segFCoreAtStart[sIdx][v] << "); ";
    os << '\n';
    os << "\n[SC-014] Anchor vs core-tap h1 (goertzelPhase, Phase 3 lock window), bar " << kVeLockBarDeg
       << " deg per window; H-2 = the same through Shape -> Fold 100 -> Character (recorded)\n";
    double maxCore = 0.0, maxChar = 0.0;
    for (const VeLockReading& l : r.lock) {
        os << "  segment " << l.segment << ' ' << veTargetName(l.target) << " voice " << l.voice << " note " << l.note
           << " (" << l.hz << " Hz):";
        for (std::size_t w = 0; w < l.dCore.size(); ++w)
            os << " dTheta[" << w << "] " << l.dCore[w];
        os << " | windows";
        for (const double s : l.sCore) {
            os << ' ' << s;
            maxCore = std::max(maxCore, s);
        }
        os << " | trend " << veWrapDeg(l.dCore.back() - l.dCore.front()) << " deg";
        os << " || H-2 char-tap windows";
        for (const double s : l.sChar) {
            os << ' ' << s;
            maxChar = std::max(maxChar, s);
        }
        os << (std::cmp_equal(l.note, PT::kVoiceEngineNoteC3) ? "  (H-2 C3 row; Phase 3 read 0.59 deg / 60 s)" : "")
           << '\n';
    }
    os << "  max core-tap window " << maxCore << " deg" << (maxCore <= kVeLockBarDeg ? "  green" : "  RED")
       << "; max char-tap window (H-2, recorded) " << maxChar << " deg\n";
    os << "  segments analysed " << r.segmentsAnalysed << " of " << p.targets.size() << '\n';
    for (const VeSegmentAudit& a : r.audits) {
        for (const int n : a.unmatchedNotes)
            os << "  segment " << a.segment << ": held note " << n << " not matched to exactly one voice  RED\n";
        for (const std::string& s : a.notes)
            os << "  segment " << a.segment << ": " << s << '\n';
    }
    os << "\nendpoint rejections " << r.endpointRejected << ", dropped note-ons " << r.droppedEvents << '\n';
    return os.str();
}

void veCheckSoak(const VeSoakPlan& p, const VeSoakResult& r) {
    const std::string report = veReport(p, r);
    veWriteEvidence(std::string("soak-") + p.name + ".txt", report);
    INFO(report);
    CHECK(r.endpointRejected == 0);
    // SC-013: every sample finite (bit test), every FR-086 tap finite.
    CHECK(r.outNonFinite == 0);
    for (std::size_t k = 0; k < kVeVoiceTapCount; ++k)
        CHECK(r.voiceBad[k] == 0);
    CHECK(r.voiceScalarBad == 0);
    for (std::size_t k = 0; k < kVeEngineTapCount; ++k)
        CHECK(r.engineBad[k] == 0);
    for (std::size_t k = 0; k < kVeGridCount; ++k)
        CHECK(r.gridBad[k] == 0);
    CHECK(r.voiceChunks > 0);
    CHECK(r.engineChunks > 0);
    CHECK(r.gridRecords > 0);
    // SC-013: state finite per interval.
    CHECK(r.poll.polls > 0);
    CHECK(r.poll.stateBad == 0);
    CHECK(r.poll.oversamplerBad == 0);
    if (!r.poll.stateAvailable || !r.poll.oversamplerAvailable)
        WARN("SC-013 state poll unavailable: ProfundumEngine declares no stateFinite() / oversamplerStateFinite() "
             "(DEFECT in run-log T019, T022 resolves)");
    // SC-013: true peak <= ceiling + 0.1 dB.
    CHECK(r.truePeakDb <= r.ceilingDb + kVeTruePeakMarginDb);
    // SC-013: DC per 10 s window <= -80 dBFS at the DC-safety tap (G11-4; the output's windows are recorded in the
    // report), no upward trend at the output.
    CHECK(!r.dcDb.empty());
    CHECK(r.dcTapDb.size() == r.dcDb.size());
    CHECK(r.dcWindowHasNote.size() == r.dcDb.size());
    for (std::size_t i = 0; i < r.dcTapDb.size(); ++i) {
        if (i < r.dcWindowHasNote.size() && r.dcWindowHasNote[i])
            continue;   // G14-2: a window with note events is recorded
        INFO("DC-tap window " << i << " (no note events)");
        CHECK(r.dcTapDb[i] <= kVeDcBarDb);
    }
    CHECK(r.trendWindows > 0);
    // G14-2 round 5: the quiet windows sit at -130 .. -140 dBFS (numerical noise); a last-mean that stays 20 dB under
    // the -80 dBFS bar is no build-up whatever the first mean read.
    CHECK((r.trendLastDb <= r.trendFirstDb + kVeTrendBarDb || r.trendLastDb <= kVeTrendFloorDb));
    // SC-014: every held note windowed in every segment, every window <= 0.5 deg.
    CHECK(r.segmentsAnalysed == p.targets.size());
    for (const VeSegmentAudit& a : r.audits)
        CHECK(a.unmatchedNotes.empty());
    CHECK(r.lock.size() == p.targets.size() * p.lockNotes.size());
    for (const VeLockReading& l : r.lock)
        for (const double s : l.sCore)
            CHECK(s <= kVeLockBarDeg);
}

/// SC-013 timeline per 15 min half: activity 77 s, gap 3 s, lock 125 s (>= 122 s) per target, then an 80 s tail:
/// 4 x (77 + 3 + 125) + 80 = 900 s. Windows at s0 + 1, 61, 121 s: two 60 s statistics per segment and voice.
[[nodiscard]] VeSoakPlan veHalfPlan(const char* name, ProfundumEngine::VoiceMode mode, std::uint32_t seed) {
    VeSoakPlan p;
    p.name = name;
    p.mode = mode;
    p.seed = seed;
    p.activitySec = {77.0, 77.0, 77.0, 77.0, 80.0};
    // G14-3 round 6 (2026-10-21): 8 s, not 3. The POLY half's Shift segment started with two voices 3.3 and 33.5
    // semitones off f_res, decaying over 1 - 2.5 s into the first lock window (94 deg): the activity's last notes
    // were still in release at s0 (ampEnv release up to 2 s) with a Drop in flight (dropTimeMs up to 4 s), and a
    // note-on on an audible voice bridges the Drop (Phase 4's C1 continuation). With every voice idle at s0 the
    // Drop snaps, as the segment's "no Drop inside" premise requires.
    p.gapSec = 8.0;
    p.lockSec = 125.0;
    p.targets = {VeTarget::Depth, VeTarget::Body, VeTarget::Edge, VeTarget::Shift};
    p.windowOffsetsSec = {1.0, 61.0, 121.0};
    if (mode == ProfundumEngine::VoiceMode::Mono)
        p.lockNotes = {PT::kVoiceEngineNoteE1};
    else
        p.lockNotes = {PT::kVoiceEngineNoteE1, PT::kVoiceEngineNoteA1, PT::kVoiceEngineNoteE2, PT::kVoiceEngineNoteC3};
    return p;
}

} // namespace

// SC-013 / SC-014 (MONO half): 15 min, seed 0x5013u.
TEST_CASE("VoiceEngine_Soak30Min_Mono", "[systems][profundum][voice-engine][sc-013][sc-014][fr-061][fr-062][long]") {
    enableFTZDAZ();
    const VeSoakPlan p = veHalfPlan("mono", ProfundumEngine::VoiceMode::Mono, 0x5013u);
    veCheckSoak(p, veRunSoak(p));
}

// SC-013 / SC-014 (POLY half): 15 min, seed 0x5014u, E1 / A1 / E2 / C3 held in each lock segment.
TEST_CASE("VoiceEngine_Soak30Min_Poly", "[systems][profundum][voice-engine][sc-013][sc-014][fr-061][fr-062][long]") {
    enableFTZDAZ();
    const VeSoakPlan p = veHalfPlan("poly", ProfundumEngine::VoiceMode::Poly, 0x5014u);
    veCheckSoak(p, veRunSoak(p));
}

// SC-013 / SC-014 smoke: 30 s MONO (2 s activity, 3 s gap, one 25 s Depth lock segment), the same gates with one
// window statistic (t0 = s0 + 1 s against s0 + 13 s; the [long] halves span 60 s). G14-2 round 6 (2026-10-21): the
// trend reads the DC-tap windows without note events, first against last, so the smoke holds two of them (10-20 s
// and 20-30 s); the 20 s smoke held one and read no trend.
TEST_CASE("VoiceEngine_SoakSmoke", "[systems][profundum][voice-engine][sc-013][sc-014][fr-061][fr-062]") {
    enableFTZDAZ();
    VeSoakPlan p;
    p.name = "smoke";
    p.mode = ProfundumEngine::VoiceMode::Mono;
    p.seed = 0x5013u;
    p.activitySec = {2.0, 0.0};
    p.gapSec = 3.0;
    p.lockSec = 25.0;
    p.targets = {VeTarget::Depth};
    p.windowOffsetsSec = {1.0, 13.0};
    p.lockNotes = {PT::kVoiceEngineNoteE1};
    veCheckSoak(p, veRunSoak(p));
}

// Main-loop probe (hidden; G14-3 round 7, 2026-10-21): one lock segment per run in POLY (2 s of activity, the 8 s
// gap, 125 s lock), each target, and the Shift segment with two voices and in MONO at A1, to locate the pitch offset
// two voices carry into the Shift segment's first window.
TEST_CASE("VoiceEngine_SoakPolyLockProbe", "[.][systems][profundum][voice-engine][probe]") {
    enableFTZDAZ();
    struct V {
        const char* name;
        ProfundumEngine::VoiceMode mode;
        VeTarget target;
        std::vector<std::uint8_t> notes;
    };
    const std::vector<V> variants{
        {"poly-shift-4", ProfundumEngine::VoiceMode::Poly, VeTarget::Shift,
         {PT::kVoiceEngineNoteE1, PT::kVoiceEngineNoteA1, PT::kVoiceEngineNoteE2, PT::kVoiceEngineNoteC3}},
        {"poly-depth-4", ProfundumEngine::VoiceMode::Poly, VeTarget::Depth,
         {PT::kVoiceEngineNoteE1, PT::kVoiceEngineNoteA1, PT::kVoiceEngineNoteE2, PT::kVoiceEngineNoteC3}},
        {"poly-shift-2", ProfundumEngine::VoiceMode::Poly, VeTarget::Shift, {PT::kVoiceEngineNoteA1, PT::kVoiceEngineNoteE2}},
        {"mono-shift-a1", ProfundumEngine::VoiceMode::Mono, VeTarget::Shift, {PT::kVoiceEngineNoteA1}},
        {"poly-4targets-short-activity", ProfundumEngine::VoiceMode::Poly, VeTarget::Shift,
         {PT::kVoiceEngineNoteE1, PT::kVoiceEngineNoteA1, PT::kVoiceEngineNoteE2, PT::kVoiceEngineNoteC3}},
        {"poly-4targets-real-activity", ProfundumEngine::VoiceMode::Poly, VeTarget::Shift,
         {PT::kVoiceEngineNoteE1, PT::kVoiceEngineNoteA1, PT::kVoiceEngineNoteE2, PT::kVoiceEngineNoteC3}},
    };
    for (const V& v : variants) {
        VeSoakPlan p;
        p.name = v.name;
        p.mode = v.mode;
        p.seed = 0x5014u;
        const bool fourTargets = std::string_view(v.name).starts_with("poly-4targets");
        const bool realActivity = std::string(v.name) == "poly-4targets-real-activity";
        if (!fourTargets)
            p.activitySec = {2.0, 2.0};
        else if (realActivity)
            p.activitySec = {77.0, 77.0, 77.0, 77.0, 80.0};
        else
            p.activitySec = {2.0, 2.0, 2.0, 2.0, 2.0};
        p.gapSec = 8.0;
        p.lockSec = 125.0;
        p.targets = fourTargets ? std::vector<VeTarget>{VeTarget::Depth, VeTarget::Body, VeTarget::Edge, VeTarget::Shift}
                                : std::vector<VeTarget>{v.target};
        p.windowOffsetsSec = {1.0, 61.0};
        p.lockNotes = v.notes;
        const VeSoakResult r = veRunSoak(p);
        const std::string rep = veReport(p, r);
        std::cout << "PROBE SOAK " << v.name << ":\n";
        std::istringstream in(rep);
        std::string line;
        while (std::getline(in, line))
            if (line.find("[record]") != std::string::npos || line.find("  segment ") != std::string::npos
                || line.find("max core-tap") != std::string::npos)
                std::cout << "  " << line << "\n";
    }
}
