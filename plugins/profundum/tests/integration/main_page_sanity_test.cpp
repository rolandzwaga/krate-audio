#include <catch2/catch_test_macros.hpp>

#include "profundum_test_fixture.h"

// Profundum_MainPageSanity — written by T031 (specs/profundum-phase12-ui/tasks.md)
//
// FR-006, FR-024; SC-006 (c); plan 6.2 "Main page render". Renders the D1 item 8 render set, the SAME list
// Profundum_MainPageRenderSet (main_page_render_set_test.cpp, T010) reads: one render per Main control (the FR-011 set,
// 17 IDs) at its registered min, default and max (the two two-value lists 1 and 1001 at both list values), the four
// combined patch renders, the B endpoint for the 105 / 1000 / 1001 renders and *Morph and motion*, Motion Amount 902 =
// 1.0 on *Morph and motion*, and the four legato pairs (412 and 1 at min / max), each at E1 and C3 (legato: one pair),
// 48 kHz, host block 512, 3 s held + 1 s tail, through a heap ProcessorFixture with an observer on the generator tap.
// The list is duplicated here (T010's builder is TU-local); the two must stay in step.
//
// Per render:
//   - every sample of the processor output is finite (Krate::DSP::detail::isFinite, fast-math immune);
//   - RMS over [0.2 s, 3 s) after note-on > -60 dBFS;
//   - sample peak <= the ID 3 default ceiling, 10^(kDefaultCeilingDb / 20) (-1 dBTP);
//   - generator tap (the observer's per-voice coreL / coreR, summed over voices): sub band [0, f_c) rho >= 0.9989
//     (the Phase 7 by-construction guarantee, specs/profundum-phase7-stereo-field/plan.md:161, :1207);
//   - processor output: StereoFieldTest::bandReading(L, R, n, fs, 0, f_c) rho >= kSubBandRhoBar (0.99) and
//     |Q| <= kSubBandQBarDb (1.0 dB), the Phase 9 SC-015 band definition. The 0.9989 bar is never applied to the output.
//   Both band checks are gated as in mono_sum_params_test.cpp:338-360: the read is valid (>= -60 dBFS band energy) and
//   the note sounding through the read window is below f_c (f_c = the resolved field.crossoverHz from onGrid).
//   Read window: [1.5 s, 1.5 s + 2^16) after note-on (inside the held part; after the legato pair's E1 release, so the
//   window's note is C3 for a legato render and the render's own note otherwise).
// Non-vacuity:
//   - control arm: Spread 801 = 1, Image Mode 800 = Wide, Gravity 802 = 0 (written under Edit Target B and then A) must
//     still pass the output bar (it is the measured guarantee); its E1 read must be gated;
//   - a mono-broken stimulus (R = -L on a copy of the default E1 render's generator tap) must FAIL the 0.9989 check.
// Over ~15 s wall clock at the gate -> re-tag [long] in a follow-up (T031 task text; T036's own-TU [long] lane).

#include "bass_stereo_field_test_helpers.h"  // read-only Phase 9 helper (tests/CMakeLists.txt include dir)

#include "parameters/global_params.h"
#include "parameters/morph_params.h"
#include "parameters/note_params.h"
#include "parameters/sound_param_row.h"
#include "parameters/sound_param_table.h"
#include "plugin_ids.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/bass_stereo_field.h>
#include <krate/dsp/systems/profundum_engine_observer.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace mpsSft = Krate::DSP::StereoFieldTest;
using MpsImg = Krate::DSP::BassStereoField::ImageMode;
using ProfundumPluginTest::ProcessorFixture;
using Steinberg::Vst::ParamID;

constexpr double kMpsFs = 48000.0;
constexpr std::size_t kMpsFsInt = 48000u;
constexpr std::size_t kMpsBlock = 512u;
constexpr std::size_t kMpsPreRoll = kMpsBlock;                           // the parameter block (T010)
constexpr std::size_t kMpsPreRollB = 3u * kMpsBlock;                     // B values / A values / rendered 1001 (T010)
constexpr std::size_t kMpsHeld = 3u * kMpsFsInt;                         // 3 s held
constexpr std::size_t kMpsTail = 1u * kMpsFsInt;                         // 1 s tail
constexpr std::size_t kMpsRender = kMpsHeld + kMpsTail;
constexpr float kMpsVelocity = 100.0f / 127.0f;
constexpr std::size_t kMpsLegatoSecondOn = 1u * kMpsFsInt;               // C3 on at 1.0 s
constexpr std::size_t kMpsLegatoFirstOff = 72000u;                       // E1 held 1.5 s
constexpr std::size_t kMpsLegatoSecondOff = kMpsHeld;                    // C3 off at 3.0 s

constexpr std::size_t kMpsRmsFrom = kMpsFsInt / 5u;                      // 0.2 s after note-on
constexpr std::size_t kMpsRmsTo = kMpsHeld;                              // 3 s
constexpr double kMpsRmsBarDb = -60.0;
constexpr std::size_t kMpsBandFrom = 72000u;                             // 1.5 s after note-on
constexpr std::size_t kMpsBandWindow = std::size_t{1} << 16;
static_assert(kMpsBandFrom + kMpsBandWindow <= kMpsHeld, "the band window lies inside the held part");
static_assert(kMpsBandFrom >= kMpsLegatoFirstOff, "the legato window starts after E1's release");
constexpr double kMpsGeneratorRhoBar = 0.9989;   // cos(pi * 0.015), Phase 7 plan.md:161

constexpr Steinberg::int16 kMpsE1 = 28;
constexpr Steinberg::int16 kMpsC3 = 48;

// -----------------------------------------------------------------------------
// The Main controls and their registered laws (as main_page_render_set_test.cpp builds them)
// -----------------------------------------------------------------------------
enum class MpsLaw : std::uint8_t { Sound, Linear01, Portamento, List };

struct MpsControl {
    ParamID id;
    const char* name;
    MpsLaw kind;
    int listCount;    // List only
    double minPlain;
    double defPlain;
    double maxPlain;
    bool twoValueList;
};

[[nodiscard]] const ::Profundum::SoundParamRow& mpsRowOf(ParamID id) {
    const ::Profundum::SoundParamRow* row = ::Profundum::soundRowOf(id);
    REQUIRE(row != nullptr);
    return *row;
}

[[nodiscard]] double mpsSoundDefault(ParamID id) {
    const auto& row = mpsRowOf(id);
    const auto& f = ::Profundum::lawOf(row.leaf);
    const std::uint32_t word = ::Profundum::kDefaultLeafBits[row.leaf];
    if (f.kind == ::Profundum::MorphState::Kind::F32) {
        return static_cast<double>(std::bit_cast<float>(word));
    }
    return static_cast<double>(word);
}

[[nodiscard]] double mpsSoundLo(ParamID id) {
    const auto& f = ::Profundum::lawOf(mpsRowOf(id).leaf);
    return f.law == ::Profundum::MorphState::Law::Stepped ? 0.0 : static_cast<double>(f.lo);
}

[[nodiscard]] double mpsSoundHi(ParamID id) {
    const auto& f = ::Profundum::lawOf(mpsRowOf(id).leaf);
    return f.law == ::Profundum::MorphState::Law::Stepped ? static_cast<double>(f.maxEnum)
                                                          : static_cast<double>(f.hi);
}

[[nodiscard]] MpsControl mpsSound(ParamID id, const char* name) {
    return MpsControl{.id = id,
                      .name = name,
                      .kind = MpsLaw::Sound,
                      .listCount = 0,
                      .minPlain = mpsSoundLo(id),
                      .defPlain = mpsSoundDefault(id),
                      .maxPlain = mpsSoundHi(id),
                      .twoValueList = false};
}

[[nodiscard]] std::vector<MpsControl> mpsMainControls() {
    using namespace ::Profundum;
    const double neutral = static_cast<double>(Krate::DSP::ProfundumMacroMatrix::kNeutral);
    // Edit Target 1001 FIRST (D1 8.1 write order), then the 8.2 table order.
    return {
        MpsControl{.id = kMorphEditTargetId,
                   .name = "EditTarget",
                   .kind = MpsLaw::List,
                   .listCount = kNumEditTargets,
                   .minPlain = 0.0,
                   .defPlain = static_cast<double>(kDefaultEditTarget),
                   .maxPlain = static_cast<double>(kNumEditTargets - 1),
                   .twoValueList = true},
        MpsControl{.id = kVoiceModeId,
                   .name = "VoiceMode",
                   .kind = MpsLaw::List,
                   .listCount = 2,
                   .minPlain = 0.0,
                   .defPlain = static_cast<double>(kDefaultVoiceModeIndex),
                   .maxPlain = 1.0,
                   .twoValueList = true},
        MpsControl{.id = kMacroWeightId,
                   .name = "Weight",
                   .kind = MpsLaw::Linear01,
                   .listCount = 0,
                   .minPlain = 0.0,
                   .defPlain = neutral,
                   .maxPlain = 1.0,
                   .twoValueList = false},
        MpsControl{.id = kMacroDarkBrightId,
                   .name = "DarkBright",
                   .kind = MpsLaw::Linear01,
                   .listCount = 0,
                   .minPlain = 0.0,
                   .defPlain = neutral,
                   .maxPlain = 1.0,
                   .twoValueList = false},
        MpsControl{.id = kMacroCleanViolentId,
                   .name = "CleanViolent",
                   .kind = MpsLaw::Linear01,
                   .listCount = 0,
                   .minPlain = 0.0,
                   .defPlain = neutral,
                   .maxPlain = 1.0,
                   .twoValueList = false},
        mpsSound(kMassId, "Mass"),
        MpsControl{.id = kMorphPositionId,
                   .name = "BassMorph",
                   .kind = MpsLaw::Linear01,
                   .listCount = 0,
                   .minPlain = 0.0,
                   .defPlain = static_cast<double>(kDefaultMorphPosition),
                   .maxPlain = 1.0,
                   .twoValueList = false},
        mpsSound(kCoreDepthId, "Depth"),
        mpsSound(kCoreBodyId, "Body"),
        mpsSound(kCoreEdgeId, "Edge"),
        mpsSound(kAnchorWaveformId, "AnchorCharacter"),
        mpsSound(kAnchorLevelId, "AnchorLevel"),
        mpsSound(kDropAmountId, "Drop"),
        MpsControl{.id = kNotePortamentoId,
                   .name = "Glide",
                   .kind = MpsLaw::Portamento,
                   .listCount = 0,
                   .minPlain = kPortamentoLaw.lo,
                   .defPlain = kPortamentoLaw.lo,
                   .maxPlain = kPortamentoLaw.hi,
                   .twoValueList = false},
        mpsSound(kFoldAmountId, "Fold"),
        mpsSound(kMotionSourceId, "MotionSource"),
        MpsControl{.id = kMorphAnchorModeId,
                   .name = "MorphBehaviour",
                   .kind = MpsLaw::List,
                   .listCount = kNumAnchorMorphModes,
                   .minPlain = 0.0,
                   .defPlain = static_cast<double>(kDefaultAnchorMorphModeIndex),
                   .maxPlain = static_cast<double>(kNumAnchorMorphModes - 1),
                   .twoValueList = false},
    };
}

[[nodiscard]] double mpsNormalised(const MpsControl& c, double plain) {
    switch (c.kind) {
        case MpsLaw::Sound:
            return ::Profundum::normalizedFromPlain(::Profundum::lawOf(mpsRowOf(c.id).leaf), plain);
        case MpsLaw::Linear01:
            return ::Profundum::normalizedFromPlain(
                ::Profundum::PlainLaw{.lo = 0.0, .hi = 1.0, .logOffset = 0.0, .logarithmic = false}, plain);
        case MpsLaw::Portamento:
            return ::Profundum::normalizedFromPlain(::Profundum::kPortamentoLaw, plain);
        case MpsLaw::List:
            return std::round(plain) / static_cast<double>(c.listCount - 1);
    }
    return 0.0;
}

// -----------------------------------------------------------------------------
// The generator-tap observer: per-voice coreL / coreR summed into buffers indexed by the record clock (the voice's
// pos_ is the engine's absolute sample, profundum_voice.h:324, profundum_engine.h:519-521), plus the resolved field.
// Never asserts or allocates in a callback.
// -----------------------------------------------------------------------------
class MpsObserver final : public Krate::DSP::ProfundumEngineObserver {
public:
    std::vector<float> coreL, coreR;
    std::size_t voiceChunks = 0;
    std::size_t droppedSamples = 0;
    bool haveGrid = false;
    MpsImg lastMode = MpsImg::Spread;
    float lastCrossoverHz = 0.0f;

    explicit MpsObserver(std::size_t total) : coreL(total, 0.0f), coreR(total, 0.0f) {}

    void onGrid(const Krate::DSP::ProfundumGridRecord& r) noexcept override {
        if (r.resolved == nullptr) {
            return;
        }
        haveGrid = true;
        lastMode = r.resolved->state.field.mode;
        lastCrossoverHz = r.resolved->state.field.crossoverHz;
    }

    void onVoiceChunk(const Krate::DSP::ProfundumVoiceTaps& t) noexcept override {
        ++voiceChunks;
        const std::size_t m = std::min({t.n, t.coreL.size(), t.coreR.size()});
        for (std::size_t i = 0; i < m; ++i) {
            const std::uint64_t at = t.sample + i;
            if (at < coreL.size()) {
                const auto k = static_cast<std::size_t>(at);
                coreL[k] += t.coreL[i];
                coreR[k] += t.coreR[i];
            } else {
                ++droppedSamples;
            }
        }
    }
};

// -----------------------------------------------------------------------------
// The two sub-band checks (gated as mono_sum_params_test.cpp:338-360)
// -----------------------------------------------------------------------------
struct MpsBandCheck {
    mpsSft::BandReading r;
    bool gated = false;
    bool pass = true;
};

[[nodiscard]] double mpsHzOf(Steinberg::int16 pitch) noexcept {
    return 440.0 * std::pow(2.0, (static_cast<double>(pitch) - 69.0) / 12.0);
}

/// The Phase 7 generator guarantee: rho >= 0.9989 on the sub band [0, f_c) when gated.
[[nodiscard]] MpsBandCheck mpsGeneratorCheck(const float* L, const float* R, double fc, Steinberg::int16 pitch) {
    MpsBandCheck c;
    c.r = mpsSft::bandReading(L, R, kMpsBandWindow, kMpsFs, 0.0, fc);
    c.gated = mpsHzOf(pitch) < fc && c.r.valid;
    c.pass = !c.gated || c.r.rho >= kMpsGeneratorRhoBar;
    return c;
}

/// The Phase 9 full-engine pair on the processor output: rho >= 0.99 and |Q| <= 1.0 dB when gated.
[[nodiscard]] MpsBandCheck mpsOutputCheck(const float* L, const float* R, double fc, Steinberg::int16 pitch) {
    MpsBandCheck c;
    c.r = mpsSft::bandReading(L, R, kMpsBandWindow, kMpsFs, 0.0, fc);
    c.gated = mpsHzOf(pitch) < fc && c.r.valid;
    c.pass = !c.gated || (c.r.rho >= mpsSft::kSubBandRhoBar && std::abs(c.r.q) <= mpsSft::kSubBandQBarDb);
    return c;
}

// -----------------------------------------------------------------------------
// One render
// -----------------------------------------------------------------------------
struct MpsValue {
    MpsControl control;
    double plain;
};

struct MpsRenderSpec {
    std::string stem;
    std::vector<double> plain;      // one value per mpsMainControls() entry
    std::vector<MpsValue> bValues;  // written under Edit Target B first; empty: no B endpoint block
    std::vector<MpsValue> extras;   // non-Main IDs written with the Main values (under the rendered target)
};

struct MpsNote {
    Steinberg::int16 pitch;
    std::size_t onAt;   // relative to the end of the pre-roll
    std::size_t offAt;
};

struct MpsResult {
    std::size_t nonFinite = 0;
    double rmsDb = -300.0;
    double peak = 0.0;
    MpsBandCheck generator;
    MpsBandCheck output;
    double fc = 0.0;
    MpsImg mode = MpsImg::Spread;
    std::size_t droppedTapSamples = 0;
    std::vector<float> coreWindowL;  // the generator tap's read window (kept for the non-vacuity stimulus)
};

[[nodiscard]] MpsResult mpsRenderOne(const std::vector<MpsControl>& controls, const MpsRenderSpec& spec,
                                     const std::vector<MpsNote>& notes, Steinberg::int16 windowPitch) {
    const bool withB = !spec.bValues.empty();
    const std::size_t preRoll = withB ? kMpsPreRollB : kMpsPreRoll;
    const std::size_t total = preRoll + kMpsRender;

    auto fx = std::make_unique<ProcessorFixture>();
    MpsObserver obs(total);
    fx->proc->setEngineObserverForTest(&obs);  // attached before setupProcessing (FR-067)
    REQUIRE(fx->prepare(kMpsFs, static_cast<Steinberg::int32>(kMpsBlock)) == Steinberg::kResultOk);

    using Ev = ProcessorFixture::ScriptedEvent;
    std::vector<Ev> script;
    script.reserve(controls.size() + spec.bValues.size() + spec.extras.size() + 3u + 2u * notes.size());
    auto param = [&script](std::size_t sample, const MpsControl& c, double plain) {
        Ev e{};
        e.sample = static_cast<Steinberg::int64>(sample);
        e.kind = Ev::Kind::Param;
        e.id = c.id;
        e.value = mpsNormalised(c, plain);
        script.push_back(e);
    };
    constexpr double kEditA = 0.0;
    constexpr double kEditB = 1.0;
    // controls[0] is Edit Target 1001. The processor applies the LAST point of each queue per block and 1001 before
    // the others (processor.cpp:665-701), so each step has its own block (T010).
    const std::size_t mainBlock = withB ? kMpsBlock : 0u;
    if (withB) {
        param(0u, controls[0], kEditB);
        for (const MpsValue& v : spec.bValues) {
            param(0u, v.control, v.plain);
        }
        param(mainBlock, controls[0], kEditA);
    } else {
        param(mainBlock, controls[0], spec.plain[0]);
    }
    for (std::size_t i = 1; i < controls.size(); ++i) {
        param(mainBlock, controls[i], spec.plain[i]);
    }
    for (const MpsValue& x : spec.extras) {
        param(mainBlock, x.control, x.plain);
    }
    if (withB) {
        param(2u * kMpsBlock, controls[0], spec.plain[0]);
    }
    for (const MpsNote& n : notes) {
        Ev on{};
        on.sample = static_cast<Steinberg::int64>(preRoll + n.onAt);
        on.kind = Ev::Kind::NoteOn;
        on.pitch = n.pitch;
        on.velocity = kMpsVelocity;
        script.push_back(on);
        Ev off{};
        off.sample = static_cast<Steinberg::int64>(preRoll + n.offAt);
        off.kind = Ev::Kind::NoteOff;
        off.pitch = n.pitch;
        script.push_back(off);
    }

    fx->renderScript(script, total, kMpsBlock);

    // Detach while inactive (FR-067), before the observer goes out of scope.
    fx->proc->setActive(0u);
    fx->proc->setEngineObserverForTest(nullptr);

    const std::vector<float>& L = fx->capturedL;
    const std::vector<float>& R = fx->capturedR;
    REQUIRE(L.size() == total);
    REQUIRE(R.size() == total);
    REQUIRE(obs.voiceChunks > 0u);
    REQUIRE(obs.haveGrid);

    MpsResult res;
    for (std::size_t i = 0; i < total; ++i) {
        const bool finiteL = Krate::DSP::detail::isFinite(L[i]);
        const bool finiteR = Krate::DSP::detail::isFinite(R[i]);
        if (!finiteL || !finiteR) {
            ++res.nonFinite;
            continue;
        }
        res.peak = std::max({res.peak, std::abs(static_cast<double>(L[i])), std::abs(static_cast<double>(R[i]))});
    }

    double sumSq = 0.0;
    for (std::size_t i = preRoll + kMpsRmsFrom; i < preRoll + kMpsRmsTo; ++i) {
        const double l = static_cast<double>(L[i]);
        const double r = static_cast<double>(R[i]);
        sumSq += 0.5 * (l * l + r * r);
    }
    const double rms = std::sqrt(sumSq / static_cast<double>(kMpsRmsTo - kMpsRmsFrom));
    res.rmsDb = rms > 0.0 ? 20.0 * std::log10(rms) : -300.0;

    res.fc = static_cast<double>(obs.lastCrossoverHz);
    res.mode = obs.lastMode;
    const std::size_t from = preRoll + kMpsBandFrom;
    REQUIRE(from + kMpsBandWindow <= total);
    res.generator = mpsGeneratorCheck(obs.coreL.data() + from, obs.coreR.data() + from, res.fc, windowPitch);
    res.output = mpsOutputCheck(L.data() + from, R.data() + from, res.fc, windowPitch);
    res.coreWindowL.assign(obs.coreL.begin() + static_cast<std::ptrdiff_t>(from),
                           obs.coreL.begin() + static_cast<std::ptrdiff_t>(from + kMpsBandWindow));
    res.droppedTapSamples = obs.droppedSamples;
    return res;
}

/// Checks one result against every per-render bar; prints one CSV line.
void mpsCheck(const std::string& label, const MpsResult& r) {
    const double ceiling = std::pow(10.0, static_cast<double>(::Profundum::kDefaultCeilingDb) / 20.0);
    std::cout << "SC-006c," << label << ",rms_db," << r.rmsDb << ",peak," << r.peak << ",fc_hz," << r.fc
              << ",gen_gated," << (r.generator.gated ? 1 : 0) << ",gen_rho," << r.generator.r.rho << ",out_gated,"
              << (r.output.gated ? 1 : 0) << ",out_rho," << r.output.r.rho << ",out_q_db," << r.output.r.q << '\n';
    INFO("render " << label << ": RMS " << r.rmsDb << " dBFS (bar > " << kMpsRmsBarDb << "), peak " << r.peak
                   << " (bar <= " << ceiling << "), f_c " << r.fc << " Hz; generator gated " << r.generator.gated
                   << " rho " << r.generator.r.rho << " (bar >= " << kMpsGeneratorRhoBar << "); output gated "
                   << r.output.gated << " rho " << r.output.r.rho << " (bar >= " << mpsSft::kSubBandRhoBar
                   << ") Q " << r.output.r.q << " dB (bar |Q| <= " << mpsSft::kSubBandQBarDb
                   << "); generator-tap samples past the buffer " << r.droppedTapSamples);
    CHECK(r.droppedTapSamples == 0u);
    CHECK(r.nonFinite == 0u);
    CHECK(r.rmsDb > kMpsRmsBarDb);
    CHECK(r.peak <= ceiling);
    CHECK(r.generator.pass);
    CHECK(r.output.pass);
}

}  // namespace

TEST_CASE("Profundum_MainPageSanity", "[profundum][ui][render]") {
    const std::vector<MpsControl> controls = mpsMainControls();
    REQUIRE(controls.size() == 17u);  // the FR-011 set (design/main-page.md 3.1)

    std::vector<double> defaults;
    defaults.reserve(controls.size());
    for (const auto& c : controls) {
        defaults.push_back(c.defPlain);
    }

    using namespace ::Profundum;
    auto indexOf = [&controls](ParamID id) {
        for (std::size_t i = 0; i < controls.size(); ++i) {
            if (controls[i].id == id) {
                return i;
            }
        }
        FAIL("Main control " << id << " missing");
        return std::size_t{0};
    };

    // The B endpoint (D1 8.4 item 1, ruled 2026-10-23): the *Growl* column (Edge 70 %, Fold 60, Mass 70).
    const std::vector<MpsValue> kBEndpoint{{.control = controls[indexOf(kCoreEdgeId)], .plain = 0.7},
                                           {.control = controls[indexOf(kFoldAmountId)], .plain = 60.0},
                                           {.control = controls[indexOf(kMassId)], .plain = 70.0}};
    auto isMorphControl = [](ParamID id) {
        return id == kMorphPositionId || id == kMorphAnchorModeId || id == kMorphEditTargetId;
    };

    std::vector<MpsRenderSpec> renders;
    for (std::size_t i = 0; i < controls.size(); ++i) {
        const MpsControl& c = controls[i];
        const std::string id = std::to_string(c.id);
        MpsRenderSpec lo{.stem = id + "_min", .plain = defaults, .bValues = {}, .extras = {}};
        lo.plain[i] = c.minPlain;
        renders.push_back(lo);
        if (!c.twoValueList) {
            MpsRenderSpec def{.stem = id + "_def", .plain = defaults, .bValues = {}, .extras = {}};
            def.plain[i] = c.defPlain;
            renders.push_back(def);
        }
        MpsRenderSpec hi{.stem = id + "_max", .plain = defaults, .bValues = {}, .extras = {}};
        hi.plain[i] = c.maxPlain;
        renders.push_back(hi);
        if (isMorphControl(c.id)) {
            const std::size_t written = c.twoValueList ? 2u : 3u;
            for (std::size_t k = renders.size() - written; k < renders.size(); ++k) {
                renders[k].bValues = kBEndpoint;
            }
        }
    }

    struct PatchValue {
        ParamID id;
        double plain;
    };
    auto patch = [&](const char* stem, std::initializer_list<PatchValue> values) -> MpsRenderSpec& {
        MpsRenderSpec r{.stem = std::string(stem) + "_patch", .plain = defaults, .bValues = {}, .extras = {}};
        for (const PatchValue& v : values) {
            r.plain[indexOf(v.id)] = v.plain;
        }
        renders.push_back(r);
        return renders.back();
    };
    constexpr double kMotionCycle = 2.0;        // Off / Env / Cycle / Drift (motion_params.h:37-38)
    constexpr double kMorphMotionAmount = 1.0;  // D1 8.4 item 2 (ruled 2026-10-23); 902 is not a Main control
    patch("deep_sub", {{.id = kMacroWeightId, .plain = 0.8},
                       {.id = kMacroCleanViolentId, .plain = 0.3},
                       {.id = kMassId, .plain = 30.0},
                       {.id = kFoldAmountId, .plain = 0.0}});
    patch("round_bass",
          {{.id = kMassId, .plain = 50.0}, {.id = kCoreBodyId, .plain = 0.7}, {.id = kCoreEdgeId, .plain = 0.3}});
    patch("growl", {{.id = kMacroCleanViolentId, .plain = 0.8},
                    {.id = kMassId, .plain = 70.0},
                    {.id = kCoreEdgeId, .plain = 0.7},
                    {.id = kFoldAmountId, .plain = 60.0}});
    {
        MpsRenderSpec& morphMotion = patch(
            "morph_motion", {{.id = kMorphPositionId, .plain = 0.5}, {.id = kMotionSourceId, .plain = kMotionCycle}});
        morphMotion.bValues = kBEndpoint;
        morphMotion.extras.push_back(
            MpsValue{.control = mpsSound(kMotionAmountId, "MotionAmount"), .plain = kMorphMotionAmount});
    }
    REQUIRE(renders.size() == 15u * 3u + 2u * 2u + 4u);  // the same count T010 asserts

    // Legato pairs (D1 8.4 item 3): 412 and 1 at min / max.
    std::vector<MpsRenderSpec> legato;
    for (const ParamID id : {static_cast<ParamID>(kNotePortamentoId), static_cast<ParamID>(kVoiceModeId)}) {
        const std::size_t i = indexOf(id);
        MpsRenderSpec lo{.stem = std::to_string(id) + "_min", .plain = defaults, .bValues = {}, .extras = {}};
        lo.plain[i] = controls[i].minPlain;
        legato.push_back(lo);
        MpsRenderSpec hi{.stem = std::to_string(id) + "_max", .plain = defaults, .bValues = {}, .extras = {}};
        hi.plain[i] = controls[i].maxPlain;
        legato.push_back(hi);
    }
    REQUIRE(legato.size() == 4u);

    struct NoteName {
        Steinberg::int16 pitch;
        const char* name;
    };
    constexpr std::array<NoteName, 2> kNotes{{{.pitch = kMpsE1, .name = "E1"}, {.pitch = kMpsC3, .name = "C3"}}};

    std::vector<float> defaultE1CoreL;  // the mono-broken stimulus source: the all-default E1 render's generator tap
    double defaultE1Fc = 0.0;
    bool defaultE1GeneratorGated = false;
    bool defaultE1GeneratorPass = false;
    std::size_t gatedGenerator = 0;
    std::size_t gatedOutput = 0;
    for (const MpsRenderSpec& r : renders) {
        for (const NoteName& n : kNotes) {
            const std::string label = r.stem + "_" + n.name;
            const MpsResult res =
                mpsRenderOne(controls, r, {MpsNote{.pitch = n.pitch, .onAt = 0u, .offAt = kMpsHeld}}, n.pitch);
            mpsCheck(label, res);
            gatedGenerator += res.generator.gated ? 1u : 0u;
            gatedOutput += res.output.gated ? 1u : 0u;
            if (label == "100_def_E1") {  // Weight at its default: every Main control at its default, no B block
                defaultE1CoreL = res.coreWindowL;
                defaultE1Fc = res.fc;
                defaultE1GeneratorGated = res.generator.gated;
                defaultE1GeneratorPass = res.generator.pass;
            }
        }
    }
    const std::vector<MpsNote> kLegatoPair{
        MpsNote{.pitch = kMpsE1, .onAt = 0u, .offAt = kMpsLegatoFirstOff},
        MpsNote{.pitch = kMpsC3, .onAt = kMpsLegatoSecondOn, .offAt = kMpsLegatoSecondOff}};
    for (const MpsRenderSpec& r : legato) {
        const MpsResult res = mpsRenderOne(controls, r, kLegatoPair, kMpsC3);  // C3 sounds through the read window
        mpsCheck(r.stem + "_legato", res);
        gatedGenerator += res.generator.gated ? 1u : 0u;
        gatedOutput += res.output.gated ? 1u : 0u;
    }
    INFO("gated generator reads " << gatedGenerator << ", gated output reads " << gatedOutput);
    CHECK(gatedGenerator > 0u);  // the bars were applied somewhere
    CHECK(gatedOutput > 0u);

    // Non-vacuity 1: the control arm (Spread 1, WIDE, Gravity 0 under B and then A) still passes the output bar,
    // and its E1 read is gated (the bar was applied).
    {
        const MpsControl spread = mpsSound(kFieldSpreadId, "Spread");
        const MpsControl image = mpsSound(kFieldImageModeId, "ImageMode");
        const MpsControl gravity = mpsSound(kFieldGravityId, "Gravity");
        const double kWide = static_cast<double>(static_cast<int>(MpsImg::Wide));
        const std::vector<MpsValue> field{
            {.control = spread, .plain = 1.0}, {.control = image, .plain = kWide}, {.control = gravity, .plain = 0.0}};
        MpsRenderSpec arm{.stem = "control_wide",
                          .plain = defaults,
                          .bValues = field,
                          .extras = field};  // under B (block 0) and A (block 1)
        for (const NoteName& n : kNotes) {
            const std::string label = arm.stem + "_" + n.name;
            const MpsResult res =
                mpsRenderOne(controls, arm, {MpsNote{.pitch = n.pitch, .onAt = 0u, .offAt = kMpsHeld}}, n.pitch);
            INFO("control arm " << label << " resolved image mode " << static_cast<int>(res.mode));
            REQUIRE(res.mode == MpsImg::Wide);
            mpsCheck(label, res);
            if (n.pitch == kMpsE1) {
                CHECK(res.output.gated);
            }
        }
    }

    // Non-vacuity 2: a mono-broken generator stimulus (R = -L on the default E1 tap copy) fails the 0.9989 check.
    {
        REQUIRE(defaultE1CoreL.size() == kMpsBandWindow);
        std::vector<float> brokenR(defaultE1CoreL.size());
        std::transform(defaultE1CoreL.begin(), defaultE1CoreL.end(), brokenR.begin(), [](float x) { return -x; });
        const MpsBandCheck broken = mpsGeneratorCheck(defaultE1CoreL.data(), brokenR.data(), defaultE1Fc, kMpsE1);
        INFO("mono-broken generator stimulus: gated " << broken.gated << ", rho " << broken.r.rho << " (f_c "
                                                       << defaultE1Fc << " Hz)");
        REQUIRE(defaultE1GeneratorGated);  // the intact default E1 tap is read ...
        CHECK(defaultE1GeneratorPass);     // ... and passes
        CHECK(broken.gated);
        CHECK_FALSE(broken.pass);  // the checker can fail
    }
}
